#define _GNU_SOURCE
#include "wspolne.h"

int semid = -1;
int shmid = -1;
int semid_limits = -1;
int msgid_stat = -1;

volatile sig_atomic_t stan_pacjenta = STAN_PRZED_SOR;
volatile sig_atomic_t ewakuacja_flaga = 0;

int sem_op_miejsca = 1;
int potrzebny_rodzic = 0;
pthread_t rodzic_thread;
volatile int rodzic_utworzony = 0;

void handle_ewakuacja(int sig) {
    ewakuacja_flaga = 1;
}

void wykonaj_ewakuacje_i_wyjdz()
{
    StanSOR *stan = (StanSOR*)shmat(shmid, NULL, 0);
    
    if (stan != (void*)-1) {
        struct sembuf lock = {SEM_DOSTEP_PAMIEC, -1, SEM_UNDO};
        struct sembuf unlock = {SEM_DOSTEP_PAMIEC, 1, SEM_UNDO};
        
        semop(semid, &lock, 1);
        
        if (stan_pacjenta == STAN_PRZED_SOR) {
            stan->ewakuowani_sprzed_sor += sem_op_miejsca;
        } 
        else if (stan_pacjenta == STAN_W_POCZEKALNI) {
            stan->ewakuowani_z_poczekalni += sem_op_miejsca;
            
            if (stan->dlugosc_kolejki_rejestracji > 0) 
                stan->dlugosc_kolejki_rejestracji--;
        }
        
        semop(semid, &unlock, 1);
        shmdt(stan);
    }
    
    if (potrzebny_rodzic && rodzic_utworzony) {
        pthread_cancel(rodzic_thread);
        pthread_join(rodzic_thread, NULL);
    }

    if (stan_pacjenta == STAN_W_POCZEKALNI) {
        struct sembuf ewak = {SEM_MIEJSCA_SOR, sem_op_miejsca, SEM_UNDO};
        semop(semid, &ewak, 1);
    }


    _exit(sem_op_miejsca);
}

#define CHECK_EWAKUACJA() if(ewakuacja_flaga) wykonaj_ewakuacje_i_wyjdz()

void* watek_rodzic(void* arg) {
    while(1) { sleep(1); pthread_testcancel(); }
    return NULL;
}

void lock_limit(int sem_indeks) {
    struct sembuf operacja = {sem_indeks, -1, SEM_UNDO};
    while (semop(semid_limits, &operacja, 1) == -1) {
        if (errno == EINTR) { CHECK_EWAKUACJA(); continue; }
        break;
    }
}

void unlock_limit(int sem_indeks) {
    struct sembuf operacja = {sem_indeks, 1, SEM_UNDO};
    semop(semid_limits, &operacja, 1);
}

int main(int argc, char *argv[])
{
    struct sigaction sa;
    sa.sa_handler = handle_ewakuacja;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    
    srand(time(NULL) ^ getpid());
    stan_pacjenta = STAN_PRZED_SOR;

    shmid = shmget(ftok(FILE_KEY, ID_SHM_MEM), 0, 0);
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
    }

    CHECK_EWAKUACJA();

    // 1. Próba wejścia (STAN_PRZED_SOR)
    struct sembuf wejscie = {SEM_MIEJSCA_SOR, -sem_op_miejsca, SEM_UNDO};
    while (semop(semid, &wejscie, 1) == -1) {
        if (errno == EINTR) { CHECK_EWAKUACJA(); continue; }
        exit(1);
    }
    
    // Udało się wejść
    stan_pacjenta = STAN_W_POCZEKALNI;
    CHECK_EWAKUACJA();

    // Zwiększ kolejkę w pamięci
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
    while (msgsnd(rej_msgid, &msg, sizeof(msg)-sizeof(long), 0) == -1) {
        if (errno == EINTR) { CHECK_EWAKUACJA(); continue; }
        unlock_limit(SLIMIT_REJESTRACJA); exit(1);
    }
    while (msgrcv(rej_msgid, &msg, sizeof(msg)-sizeof(long), mpid, 0) == -1) {
        if (errno == EINTR) { CHECK_EWAKUACJA(); continue; }
        unlock_limit(SLIMIT_REJESTRACJA); exit(1);
    }
    unlock_limit(SLIMIT_REJESTRACJA);
    CHECK_EWAKUACJA();

    if (stan != (void*)-1) { // Aktualizacja kolejki (ponowne podłączenie bo mogło minąć dużo czasu)
        stan = (StanSOR*)shmat(shmid, NULL, 0);
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
    while (msgsnd(poz_id, &msg, sizeof(msg)-sizeof(long), 0) == -1) {
        if (errno == EINTR) { CHECK_EWAKUACJA(); continue; }
    }
    while (msgrcv(poz_id, &msg, sizeof(msg)-sizeof(long), mpid, 0) == -1) {
        if (errno == EINTR) { CHECK_EWAKUACJA(); continue; }
    }
    unlock_limit(SLIMIT_POZ);
    CHECK_EWAKUACJA();

    // 4. Specjalista
    if (msg.typ_lekarza > 0) {
        int spec_id = msg.typ_lekarza;
        int qid = msgget(ftok(FILE_KEY, (spec_id==1?'K':spec_id==2?'N':spec_id==3?'L':spec_id==4?'C':spec_id==5?'O':'D')), 0);
        msg.mtype = msg.kolor;
        
        lock_limit(spec_id + 1); 
        while (msgsnd(qid, &msg, sizeof(msg)-sizeof(long), 0) == -1) {
             if (errno == EINTR) { CHECK_EWAKUACJA(); continue; }
        }
        while (msgrcv(qid, &msg, sizeof(msg)-sizeof(long), mpid, 0) == -1) {
             if (errno == EINTR) { CHECK_EWAKUACJA(); continue; }
        }
        unlock_limit(spec_id + 1);
    }
    CHECK_EWAKUACJA();

    // 5. Statystyki i wyjście
    StatystykaPacjenta stat;
    stat.mtype = 1;
    stat.czy_vip = vip;
    stat.kolor = msg.kolor;
    stat.typ_lekarza = msg.typ_lekarza;
    stat.skierowanie = msg.skierowanie;
    msgsnd(msgid_stat, &stat, sizeof(stat)-sizeof(long), 0);

    // Koniec - pacjent wychodzi normalnie (STAN_WYCHODZI)
    stan_pacjenta = STAN_WYCHODZI;
    if (potrzebny_rodzic && rodzic_utworzony) {
        pthread_cancel(rodzic_thread);
        pthread_join(rodzic_thread, NULL);
    }
    
    struct sembuf wyjscie = {SEM_MIEJSCA_SOR, sem_op_miejsca, SEM_UNDO};
    semop(semid, &wyjscie, 1);
    
    return 0;
}