#define _GNU_SOURCE
#include "wspolne.h"

int semid = -1;
int semid_limits = -1;
int msgid_stat = -1;
int shmid = -1;

volatile sig_atomic_t stan_pacjenta = STAN_PRZED_SOR;
int sem_op_miejsca = 1;
int potrzebny_rodzic = 0;
pthread_t rodzic_thread;
volatile int rodzic_utworzony = 0;

// Funkcja pomocnicza do aktualizacji liczników w SHM
// Wywoływana w normalnym trybie pracy (nie w handlerze)
void aktualizuj_liczniki(int zmiana_przed, int zmiana_wew, int zmiana_kolejki_rej) {
    StanSOR *stan = (StanSOR*)shmat(shmid, NULL, 0);
    if (stan != (void*)-1) {
        struct sembuf lock = {SEM_DOSTEP_PAMIEC, -1, SEM_UNDO};
        struct sembuf unlock = {SEM_DOSTEP_PAMIEC, 1, SEM_UNDO};
        
        // Czekamy na dostęp do pamięci
        while(semop(semid, &lock, 1) == -1) { 
            if (errno == EINTR) continue; // Retry na sygnałach
            return;
        }
        
        // Sekcja krytyczna
        stan->pacjenci_przed_sor += zmiana_przed;
        stan->pacjenci_w_poczekalni += zmiana_wew;
        stan->dlugosc_kolejki_rejestracji += zmiana_kolejki_rej;
        
        semop(semid, &unlock, 1);
        shmdt(stan);
    }
}

// Handler SIGTERM - wywoływany przez Generator podczas ewakuacji
void handle_kill(int sig) {
    // 1. Sprzątanie wątku opiekuna (jeśli jest)
    if (potrzebny_rodzic && rodzic_utworzony) {
        pthread_cancel(rodzic_thread);
        pthread_join(rodzic_thread, NULL);
    }
    
    // 2. Odłączenie pamięci (ważne, żeby nie wyciekała)
    // (W nowoczesnych OS exit to robi, ale dla porządku...)
    // shmdt... nie mamy wskaźnika globalnego, ale to ok.

    // 3. Exit code w zależności od stanu
    // SEM_UNDO automatycznie zwolni miejsce w poczekalni, jeśli je zajęliśmy.
    if (stan_pacjenta == STAN_W_POCZEKALNI) {
        _exit(sem_op_miejsca); // Zwracamy wagę (1 lub 2)
    }
    
    // Jeśli byłem przed SOR (STAN_PRZED_SOR) -> zwracam 0
    _exit(0);
}

void* watek_rodzic(void* arg) {
    while(1) { sleep(1); pthread_testcancel(); }
    return NULL;
}

void lock_limit(int sem_indeks) {
    struct sembuf operacja = {sem_indeks, -1, SEM_UNDO};
    while (semop(semid_limits, &operacja, 1) == -1) { if(errno == EINTR) continue; break; }
}
void unlock_limit(int sem_indeks) {
    struct sembuf operacja = {sem_indeks, 1, SEM_UNDO};
    semop(semid_limits, &operacja, 1);
}

int main(int argc, char *argv[])
{
    signal(SIGINT, SIG_IGN);          
    signal(SIGTERM, handle_kill);     

    srand(time(NULL) ^ getpid());
    stan_pacjenta = STAN_PRZED_SOR;

    semid = semget(ftok(FILE_KEY, ID_SEM_SET), 0, 0);
    semid_limits = semget(ftok(FILE_KEY, ID_SEM_LIMITS), 0, 0);
    shmid = shmget(ftok(FILE_KEY, ID_SHM_MEM), 0, 0);
    int rej_msgid = msgget(ftok(FILE_KEY, ID_KOLEJKA_REJESTRACJA), 0);
    int poz_id = msgget(ftok(FILE_KEY, ID_KOLEJKA_POZ), 0);
    msgid_stat = msgget(ftok(FILE_KEY, ID_KOLEJKA_STATYSTYKI), 0);

    if (semid == -1 || rej_msgid == -1 || shmid == -1) exit(1);

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

    // === KROK 1: Urodzenie się ===
    // Jestem przed SOR, więc zwiększam licznik w SHM
    aktualizuj_liczniki(sem_op_miejsca, 0, 0);

    // === KROK 2: Próba wejścia ===
    struct sembuf wejscie = {SEM_MIEJSCA_SOR, -sem_op_miejsca, SEM_UNDO};
    while (semop(semid, &wejscie, 1) == -1) { 
        if (errno == EINTR) continue; 
        exit(1); 
    }
    
    // === KROK 3: Wejście fizyczne (Aktualizacja stanu) ===
    // Przeszedłem przez bramkę, więc odejmuję się z "Przed", dodaję do "Wewnatrz"
    // Dodatkowo zwiększam kolejkę do rejestracji
    aktualizuj_liczniki(-sem_op_miejsca, sem_op_miejsca, 1);
    
    // Dopiero po aktualizacji w SHM zmieniam flagę lokalną
    // Dzięki temu, jeśli SIGTERM przyjdzie przed aktualizacją SHM, exit code będzie 0,
    // a w SHM będę figurował jako "przed SOR". Wszystko się zgodzi.
    stan_pacjenta = STAN_W_POCZEKALNI;

    KomunikatPacjenta msg;
    memset(&msg, 0, sizeof(msg));
    msg.mtype = vip ? TYP_VIP : TYP_ZWYKLY;
    msg.wiek = wiek;
    msg.pacjent_pid = mpid;
    msg.czy_vip = vip;

    // === KROK 4: Rejestracja ===
    lock_limit(SLIMIT_REJESTRACJA);
    while (msgsnd(rej_msgid, &msg, sizeof(msg)-sizeof(long), 0) == -1) { if (errno == EINTR) continue; unlock_limit(SLIMIT_REJESTRACJA); exit(1); }
    while (msgrcv(rej_msgid, &msg, sizeof(msg)-sizeof(long), mpid, 0) == -1) { if (errno == EINTR) continue; unlock_limit(SLIMIT_REJESTRACJA); exit(1); }
    unlock_limit(SLIMIT_REJESTRACJA);

    // Po rejestracji: Zmniejsz kolejkę rejestracji
    aktualizuj_liczniki(0, 0, -1);

    // === KROK 5: POZ ===
    msg.mtype = 1; 
    lock_limit(SLIMIT_POZ);
    while (msgsnd(poz_id, &msg, sizeof(msg)-sizeof(long), 0) == -1) { if (errno == EINTR) continue; }
    while (msgrcv(poz_id, &msg, sizeof(msg)-sizeof(long), mpid, 0) == -1) { if (errno == EINTR) continue; }
    unlock_limit(SLIMIT_POZ);

    // === KROK 6: Specjalista ===
    if (msg.typ_lekarza > 0) {
        int spec_id = msg.typ_lekarza;
        int qid = msgget(ftok(FILE_KEY, (spec_id==1?'K':spec_id==2?'N':spec_id==3?'L':spec_id==4?'C':spec_id==5?'O':'D')), 0);
        msg.mtype = msg.kolor;
        lock_limit(spec_id + 1); 
        while (msgsnd(qid, &msg, sizeof(msg)-sizeof(long), 0) == -1) { if (errno == EINTR) continue; }
        while (msgrcv(qid, &msg, sizeof(msg)-sizeof(long), mpid, 0) == -1) { if (errno == EINTR) continue; }
        unlock_limit(spec_id + 1);
    }

    // === KROK 7: Koniec wizyty ===
    StatystykaPacjenta stat;
    stat.mtype = 1;
    stat.czy_vip = vip;
    stat.kolor = msg.kolor;
    stat.typ_lekarza = msg.typ_lekarza;
    stat.skierowanie = msg.skierowanie;
    msgsnd(msgid_stat, &stat, sizeof(stat)-sizeof(long), 0);

    // Wychodzę z SOR
    stan_pacjenta = STAN_WYCHODZI; 
    
    // Zdejmuję się z licznika "Wewnatrz"
    aktualizuj_liczniki(0, -sem_op_miejsca, 0);

    if (potrzebny_rodzic && rodzic_utworzony) {
        pthread_cancel(rodzic_thread);
        pthread_join(rodzic_thread, NULL);
    }
    
    // Zwalniam semafor (fizyczne wyjście)
    struct sembuf wyjscie = {SEM_MIEJSCA_SOR, sem_op_miejsca, SEM_UNDO};
    semop(semid, &wyjscie, 1);
    
    return 0;
}