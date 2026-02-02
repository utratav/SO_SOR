#define _GNU_SOURCE
#include "wspolne.h"

int semid = -1;
int semid_limits = -1;
int msgid_stat = -1;

// WAŻNE: Tylko lokalne zmienne w handlerze
volatile sig_atomic_t stan_pacjenta = STAN_PRZED_SOR;
int sem_op_miejsca = 1;
int potrzebny_rodzic = 0;
pthread_t rodzic_thread;
volatile int rodzic_utworzony = 0;

// Handler SIGTERM (Zabicie przez Generator)
void handle_kill(int sig) {
    // Zero operacji na semaforach czy pamięci.
    // Tylko zwracamy swoją wagę.
    
    // Jeśli byłem w środku (zająłem miejsce w semaforze) -> zwracam wagę
    if (stan_pacjenta == STAN_W_POCZEKALNI) {
        _exit(sem_op_miejsca);
    }
    
    // Jeśli byłem na zewnątrz (czekałem na semaforze) -> nic nie zająłem
    _exit(0);
}

void* watek_rodzic(void* arg) {
    while(1) { sleep(1); pthread_testcancel(); }
    return NULL;
}

// Funkcje pomocnicze do limitów
void lock_limit(int sem_indeks) {
    struct sembuf operacja = {sem_indeks, -1, SEM_UNDO};
    while (semop(semid_limits, &operacja, 1) == -1) {
        // Jeśli przerwane przez sygnał, pętla ponowi, ale jeśli to SIGTERM to handler zrobi _exit
        if(errno == EINTR) continue; 
        break;
    }
}
void unlock_limit(int sem_indeks) {
    struct sembuf operacja = {sem_indeks, 1, SEM_UNDO};
    semop(semid_limits, &operacja, 1);
}

int main(int argc, char *argv[])
{
    // KONFIGURACJA SYGNAŁÓW
    signal(SIGINT, SIG_IGN);          // Ignoruj Ctrl+C (zajmie się tym Main/Generator)
    signal(SIGTERM, handle_kill);     // Obsłuż ostateczne zabicie

    srand(time(NULL) ^ getpid());
    stan_pacjenta = STAN_PRZED_SOR;

    semid = semget(ftok(FILE_KEY, ID_SEM_SET), 0, 0);
    semid_limits = semget(ftok(FILE_KEY, ID_SEM_LIMITS), 0, 0);
    int rej_msgid = msgget(ftok(FILE_KEY, ID_KOLEJKA_REJESTRACJA), 0);
    int poz_id = msgget(ftok(FILE_KEY, ID_KOLEJKA_POZ), 0);
    msgid_stat = msgget(ftok(FILE_KEY, ID_KOLEJKA_STATYSTYKI), 0);

    if (semid == -1 || rej_msgid == -1) exit(1);

    pid_t mpid = getpid();
    int wiek = rand() % 100;
    int vip = rand() % 100 < 20;

    if (wiek < 18) {
        potrzebny_rodzic = 1;
        sem_op_miejsca = 2;
        pthread_create(&rodzic_thread, NULL, watek_rodzic, NULL);
        rodzic_utworzony = 1;
        zapisz_raport(KONSOLA, semid, "[Pacjent %d] Utworzono (Wiek: %d, VIP: %d, Opiekun TID: %lu)\n", mpid, wiek, vip, (unsigned long)rodzic_thread);
    } else {
        zapisz_raport(KONSOLA, semid, "[Pacjent %d] Utworzono (Wiek: %d, VIP: %d)\n", mpid, wiek, vip);
    }

    // 1. Wejście (Semafor główny)
    struct sembuf wejscie = {SEM_MIEJSCA_SOR, -sem_op_miejsca, SEM_UNDO};
    while (semop(semid, &wejscie, 1) == -1) {
        if (errno == EINTR) continue;
        exit(1);
    }
    
    // UDAŁO SIĘ WEJŚĆ
    stan_pacjenta = STAN_W_POCZEKALNI; // Flaga dla handlera

    // Zwiększ kolejkę rejestracji (dla logiki otwierania okienka)
    // UWAGA: To może być jedyny moment ryzykowny, ale to tylko int++
    // Przy SIGTERM nie wycofujemy tego, bo Snapshot Generatora bazuje na SEM_MIEJSCA_SOR, a nie na tej zmiennej.
    int shmid = shmget(ftok(FILE_KEY, ID_SHM_MEM), 0, 0);
    StanSOR *stan = (StanSOR*)shmat(shmid, NULL, 0);
    if (stan != (void*)-1) {
        struct sembuf lock = {SEM_DOSTEP_PAMIEC, -1, SEM_UNDO};
        struct sembuf unlock = {SEM_DOSTEP_PAMIEC, 1, SEM_UNDO};
        semop(semid, &lock, 1);
        stan->dlugosc_kolejki_rejestracji++;
        semop(semid, &unlock, 1);
        shmdt(stan);
    }

    KomunikatPacjenta msg;
    memset(&msg, 0, sizeof(msg));
    msg.mtype = vip ? TYP_VIP : TYP_ZWYKLY;
    msg.wiek = wiek;
    msg.pacjent_pid = mpid;
    msg.czy_vip = vip;

    // 2. Rejestracja
    lock_limit(SLIMIT_REJESTRACJA);
    while (msgsnd(rej_msgid, &msg, sizeof(msg)-sizeof(long), 0) == -1) { if (errno == EINTR) continue; unlock_limit(SLIMIT_REJESTRACJA); exit(1); }
    while (msgrcv(rej_msgid, &msg, sizeof(msg)-sizeof(long), mpid, 0) == -1) { if (errno == EINTR) continue; unlock_limit(SLIMIT_REJESTRACJA); exit(1); }
    unlock_limit(SLIMIT_REJESTRACJA);

    // Zmniejsz kolejkę
    if ((stan = (StanSOR*)shmat(shmid, NULL, 0)) != (void*)-1) {
        struct sembuf lock = {SEM_DOSTEP_PAMIEC, -1, SEM_UNDO};
        struct sembuf unlock = {SEM_DOSTEP_PAMIEC, 1, SEM_UNDO};
        semop(semid, &lock, 1);
        if (stan->dlugosc_kolejki_rejestracji > 0) stan->dlugosc_kolejki_rejestracji--;
        semop(semid, &unlock, 1);
        shmdt(stan);
    }

    // 3. POZ
    msg.mtype = 1; 
    lock_limit(SLIMIT_POZ);
    while (msgsnd(poz_id, &msg, sizeof(msg)-sizeof(long), 0) == -1) { if (errno == EINTR) continue; }
    while (msgrcv(poz_id, &msg, sizeof(msg)-sizeof(long), mpid, 0) == -1) { if (errno == EINTR) continue; }
    unlock_limit(SLIMIT_POZ);

    // 4. Specjalista
    if (msg.typ_lekarza > 0) {
        int spec_id = msg.typ_lekarza;
        int qid = msgget(ftok(FILE_KEY, (spec_id==1?'K':spec_id==2?'N':spec_id==3?'L':spec_id==4?'C':spec_id==5?'O':'D')), 0);
        msg.mtype = msg.kolor;
        lock_limit(spec_id + 1); 
        while (msgsnd(qid, &msg, sizeof(msg)-sizeof(long), 0) == -1) { if (errno == EINTR) continue; }
        while (msgrcv(qid, &msg, sizeof(msg)-sizeof(long), mpid, 0) == -1) { if (errno == EINTR) continue; }
        unlock_limit(spec_id + 1);
    }

    // 5. Statystyki
    StatystykaPacjenta stat;
    stat.mtype = 1;
    stat.czy_vip = vip;
    stat.kolor = msg.kolor;
    stat.typ_lekarza = msg.typ_lekarza;
    stat.skierowanie = msg.skierowanie;
    msgsnd(msgid_stat, &stat, sizeof(stat)-sizeof(long), 0);

    stan_pacjenta = STAN_WYCHODZI; // Już wychodzi, więc jak dostanie TERM to exit(0)
    
    if (potrzebny_rodzic && rodzic_utworzony) {
        pthread_cancel(rodzic_thread);
        pthread_join(rodzic_thread, NULL);
    }
    
    struct sembuf wyjscie = {SEM_MIEJSCA_SOR, sem_op_miejsca, SEM_UNDO};
    semop(semid, &wyjscie, 1);
    
    return 0;
}