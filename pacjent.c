#define _GNU_SOURCE

#include "wspolne.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <pthread.h>

int semid = -1;
int semid_limits = -1;
int shmid = -1;
StanSOR *stan_ptr = NULL;

// Stan pacjenta - gdzie się znajduje
volatile sig_atomic_t stan_pacjenta = STAN_PRZED_SOR;
volatile sig_atomic_t ewakuacja_flaga = 0;

int sem_op_miejsca = 1;       
int potrzebny_rodzic = 0;
pthread_t rodzic_thread;
unsigned long id_opiekuna = 0;
volatile sig_atomic_t rodzic_utworzony = 0;

void handle_ewakuacja(int sig)
{
    ewakuacja_flaga = 1;
}

void wykonaj_ewakuacje()
{
    if (stan_pacjenta == STAN_PRZED_SOR)
    {
        if (stan_ptr != NULL && semid != -1)
        {
            struct sembuf lock = {SEM_DOSTEP_PAMIEC, -1, 0};
            struct sembuf unlock = {SEM_DOSTEP_PAMIEC, 1, 0};
            
            if (semop(semid, &lock, 1) == 0) {
                stan_ptr->ewakuowani_sprzed_sor++;
                if (stan_ptr->liczba_przed_sor >= sem_op_miejsca)
                    stan_ptr->liczba_przed_sor -= sem_op_miejsca;
                semop(semid, &unlock, 1);
            }
        }
        
    }
    else if (stan_pacjenta == STAN_W_POCZEKALNI || stan_pacjenta == STAN_U_LEKARZA)
    {
        if (stan_ptr != NULL && semid != -1)
        {
            struct sembuf lock = {SEM_DOSTEP_PAMIEC, -1, 0};
            struct sembuf unlock = {SEM_DOSTEP_PAMIEC, 1, 0};
            
            if (semop(semid, &lock, 1) == 0) {
                if (stan_pacjenta == STAN_W_POCZEKALNI) {
                    if (stan_ptr->dlugosc_kolejki_rejestracji > 0)
                        stan_ptr->dlugosc_kolejki_rejestracji--;
                }
                
                if (stan_ptr->liczba_pacjentow_w_srodku >= sem_op_miejsca)
                    stan_ptr->liczba_pacjentow_w_srodku -= sem_op_miejsca;
                
                stan_ptr->ewakuowani_z_poczekalni++;
                
                semop(semid, &unlock, 1);
            }
        }
        
        if (semid != -1) {
            struct sembuf wyjscie = {SEM_MIEJSCA_SOR, sem_op_miejsca, 0};
            semop(semid, &wyjscie, 1);
        }
    }
    
    if (potrzebny_rodzic && rodzic_utworzony) {
        pthread_cancel(rodzic_thread);
        pthread_join(rodzic_thread, NULL);
    }
    
    if (stan_ptr != NULL) {
        shmdt(stan_ptr);
    }
}

void* watek_rodzic(void* arg)
{
    while(1) {
        sleep(1);
        pthread_testcancel();
    }
    return NULL;
}

void lock_limit(int sem_indeks)
{
    struct sembuf operacja = {sem_indeks, -1, SEM_UNDO};
    while (semop(semid_limits, &operacja, 1) == -1) {
        if (errno == EINTR) {
            if (ewakuacja_flaga) return;
            continue;
        }
        break;
    }
}

void unlock_limit(int sem_indeks)
{
    struct sembuf operacja = {sem_indeks, 1, SEM_UNDO};
    semop(semid_limits, &operacja, 1);
}

#define CHECK_EWAKUACJA() do { \
    if (ewakuacja_flaga) { \
        wykonaj_ewakuacje(); \
        exit(0); \
    } \
} while(0)

int main(int argc, char *argv[])
{
    struct sigaction sa;
    sa.sa_handler = handle_ewakuacja;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; 
    sigaction(SIGINT, &sa, NULL);
    
  
    signal(SIGTSTP, SIG_DFL);
    signal(SIGCONT, SIG_DFL);
    
    srand(time(NULL) ^ getpid());

    key_t key_sem = ftok(FILE_KEY, ID_SEM_SET);
    key_t key_limits = ftok(FILE_KEY, ID_SEM_LIMITS);
    key_t key_msg_rej = ftok(FILE_KEY, ID_KOLEJKA_REJESTRACJA);
    key_t key_msg_poz = ftok(FILE_KEY, ID_KOLEJKA_POZ);
    key_t key_shm = ftok(FILE_KEY, ID_SHM_MEM);

    semid = semget(key_sem, 0, 0);
    semid_limits = semget(key_limits, 0, 0);
    int rej_msgid = msgget(key_msg_rej, 0);
    int poz_id = msgget(key_msg_poz, 0);
    shmid = shmget(key_shm, 0, 0);

    int spec_msgids[10];
    for (int i = 0; i < 10; i++) spec_msgids[i] = -1;
    spec_msgids[1] = msgget(ftok(FILE_KEY, ID_KOL_KARDIOLOG), 0);
    spec_msgids[2] = msgget(ftok(FILE_KEY, ID_KOL_NEUROLOG), 0);
    spec_msgids[3] = msgget(ftok(FILE_KEY, ID_KOL_LARYNGOLOG), 0);
    spec_msgids[4] = msgget(ftok(FILE_KEY, ID_KOL_CHIRURG), 0);
    spec_msgids[5] = msgget(ftok(FILE_KEY, ID_KOL_OKULISTA), 0);
    spec_msgids[6] = msgget(ftok(FILE_KEY, ID_KOL_PEDIATRA), 0);

    if (semid == -1 || rej_msgid == -1 || shmid == -1 || poz_id == -1 || semid_limits == -1)
    {
        perror("blad przy podlaczaniu do ipc");
        exit(EXIT_FAILURE);
    } 

    int err = 0;
    for (int i = 1; i <= 6; i++)
    {
        if (spec_msgids[i] == -1) err = i;
    }
    if (err > 0) {perror("blad msgid dla specjalisty"); exit(EXIT_FAILURE);}

    StanSOR *stan = (StanSOR*)shmat(shmid, NULL, 0);
    if (stan == (void*)-1)
    {
        perror("blad shmat");
        exit(EXIT_FAILURE);
    }
    stan_ptr = stan;

    CHECK_EWAKUACJA();

    pid_t mpid = getpid();
    int wiek = rand() % 100;
    int vip = rand() % 100 < 20;

    potrzebny_rodzic = (wiek < 18);
    sem_op_miejsca = potrzebny_rodzic ? 2 : 1; 

    if (potrzebny_rodzic) 
    {
        if(pthread_create(&rodzic_thread, NULL, watek_rodzic, NULL) != 0) 
        {
            perror("blad tworzenia watku rodzic");            
            potrzebny_rodzic = 0; 
            sem_op_miejsca = 1;
        } 
        else 
        {           
            rodzic_utworzony = 1;
            id_opiekuna = (unsigned long)rodzic_thread;
        }
    }

    char buf[150]; 
    if (potrzebny_rodzic)
    {
        sprintf(buf, "[pacjent] id: %d --- wiek: %d --- vip: %s\t [rodzic] tid: %lu\n", 
                mpid, wiek, vip ? "tak" : "nie", id_opiekuna);
    } 
    else 
    {
        sprintf(buf, "[pacjent] id: %d --- wiek: %d --- vip: %s\n", 
                mpid, wiek, vip ? "tak" : "nie");
    }
    zapisz_raport(KONSOLA, semid, "%s", buf);

    CHECK_EWAKUACJA();

    stan_pacjenta = STAN_PRZED_SOR;
    
    struct sembuf mutex_lock = {SEM_DOSTEP_PAMIEC, -1, SEM_UNDO};
    struct sembuf mutex_unlock = {SEM_DOSTEP_PAMIEC, 1, SEM_UNDO};
    
    semop(semid, &mutex_lock, 1);
    stan->liczba_przed_sor += sem_op_miejsca;
    semop(semid, &mutex_unlock, 1);

    CHECK_EWAKUACJA();

    // Próba wejścia do poczekalni
    struct sembuf wejscie_do_poczekalni = {SEM_MIEJSCA_SOR, -sem_op_miejsca, SEM_UNDO};

    while (semop(semid, &wejscie_do_poczekalni, 1) == -1)
    {
        if (errno == EINTR) {
            CHECK_EWAKUACJA();
            continue;
        }
        perror("blad semop wejscie do poczekalni");
        wykonaj_ewakuacje();
        exit(1);
    }

    CHECK_EWAKUACJA();

    stan_pacjenta = STAN_W_POCZEKALNI;

    semop(semid, &mutex_lock, 1);
    if (stan->liczba_przed_sor >= sem_op_miejsca)
        stan->liczba_przed_sor -= sem_op_miejsca;
    stan->liczba_pacjentow_w_srodku += sem_op_miejsca;
    stan->dlugosc_kolejki_rejestracji++;    
    
    int prog_otwarcia = MAX_PACJENTOW / 2;
    if (!stan->czy_okienko_2_otwarte && stan->dlugosc_kolejki_rejestracji > prog_otwarcia)
    {
        stan->czy_okienko_2_otwarte = 1;
        zapisz_raport(KONSOLA, semid, "[REJESTRACJA] Otwieram okienko 2 (Kolejka: %d)\n", stan->dlugosc_kolejki_rejestracji);
        zapisz_raport(RAPORT_2, semid, "[REJESTRACJA] Otwieram 2 okienko | osob w kolejce: %d\n", stan->dlugosc_kolejki_rejestracji);
    }
    semop(semid, &mutex_unlock, 1);

    CHECK_EWAKUACJA();
    
    KomunikatPacjenta msg;
    memset(&msg, 0, sizeof(msg));
    msg.mtype = vip ? TYP_VIP : TYP_ZWYKLY;
    msg.wiek = wiek;
    msg.pacjent_pid = mpid;
    msg.czy_vip = vip;

    lock_limit(SLIMIT_REJESTRACJA);
    CHECK_EWAKUACJA();
    
    while (msgsnd(rej_msgid, &msg, sizeof(KomunikatPacjenta) - sizeof(long), 0) == -1)
    {
        if (errno == EINTR) {
            CHECK_EWAKUACJA();
            continue;
        }
        if (errno == EAGAIN) { usleep(10000); continue; }
        perror("blad wysylania do rejestracji");
        unlock_limit(SLIMIT_REJESTRACJA);
        wykonaj_ewakuacje();
        exit(1);
    }    
 
    while (msgrcv(rej_msgid, &msg, sizeof(KomunikatPacjenta) - sizeof(long), mpid, 0) == -1)
    {
        if (errno == EINTR) {
            CHECK_EWAKUACJA();
            continue;
        }
        perror("blad msgrcv od rejestracji");
        unlock_limit(SLIMIT_REJESTRACJA);
        wykonaj_ewakuacje();
        exit(1);
    }
    unlock_limit(SLIMIT_REJESTRACJA);

    CHECK_EWAKUACJA();

    semop(semid, &mutex_lock, 1);
    stan->dlugosc_kolejki_rejestracji--;      
    semop(semid, &mutex_unlock, 1);

    stan_pacjenta = STAN_U_LEKARZA;

    msg.mtype = 1; 
    lock_limit(SLIMIT_POZ);
    CHECK_EWAKUACJA();
    
    while (msgsnd(poz_id, &msg, sizeof(KomunikatPacjenta) - sizeof(long), 0) == -1)
    {
        if (errno == EINTR) {
            CHECK_EWAKUACJA();
            continue;
        }
        if (errno == EAGAIN) { usleep(10000); continue; }
        perror("blad msgsnd wysylania do poz");
        unlock_limit(SLIMIT_POZ);
        wykonaj_ewakuacje();
        exit(1);
    }

    while (msgrcv(poz_id, &msg, sizeof(KomunikatPacjenta) - sizeof(long), mpid, 0) == -1)
    {
        if (errno == EINTR) {
            CHECK_EWAKUACJA();
            continue;
        }
        perror("blad msgrcv od poz");
        unlock_limit(SLIMIT_POZ);
        wykonaj_ewakuacje();
        exit(1);
    }
    unlock_limit(SLIMIT_POZ);

    CHECK_EWAKUACJA();

    // ========== Specjalista (jeśli potrzebny) ==========
    if (msg.typ_lekarza > 0)
    {
        int id_spec = msg.typ_lekarza;
        msg.mtype = msg.kolor; 
        int sem_limit_indeks = -1;

        switch (id_spec) 
        {
            case LEK_KARDIOLOG:  sem_limit_indeks = SLIMIT_KARDIOLOG; break;
            case LEK_NEUROLOG:   sem_limit_indeks = SLIMIT_NEUROLOG; break;
            case LEK_LARYNGOLOG: sem_limit_indeks = SLIMIT_LARYNGOLOG; break;
            case LEK_CHIRURG:    sem_limit_indeks = SLIMIT_CHIRURG; break;
            case LEK_OKULISTA:   sem_limit_indeks = SLIMIT_OKULISTA; break;
            case LEK_PEDIATRA:   sem_limit_indeks = SLIMIT_PEDIATRA; break;
            default: break;
        }

        if (sem_limit_indeks != -1)
        {
            lock_limit(sem_limit_indeks);
            CHECK_EWAKUACJA();

            while (msgsnd(spec_msgids[id_spec], &msg, sizeof(KomunikatPacjenta) - sizeof(long), 0) == -1)
            {
                if (errno == EINTR) {
                    CHECK_EWAKUACJA();
                    continue;
                }
                if (errno == EAGAIN) { usleep(10000); continue; }
                perror("blad wysylania do specjalisty");
                unlock_limit(sem_limit_indeks);
                wykonaj_ewakuacje();
                exit(1);
            }

            while (msgrcv(spec_msgids[id_spec], &msg, sizeof(KomunikatPacjenta) - sizeof(long), mpid, 0) == -1)
            {
                if (errno == EINTR) {
                    CHECK_EWAKUACJA();
                    continue;
                }
                perror("blad msgrcv od specjalisty");
                unlock_limit(sem_limit_indeks);
                wykonaj_ewakuacje();
                exit(1);
            }

            unlock_limit(sem_limit_indeks);
        }
    }

    CHECK_EWAKUACJA();

    // ========== FAZA 4: Wychodzenie ==========
    stan_pacjenta = STAN_WYCHODZI;

    semop(semid, &mutex_lock, 1);
    stan->liczba_pacjentow_w_srodku -= sem_op_miejsca;
    stan->obs_pacjenci++;
    if(msg.czy_vip) stan->ile_vip++;
    stan->obs_kolory[msg.kolor]++;
    if (!(msg.typ_lekarza))
    {
        stan->obs_dom_poz++;
        stan->decyzja[msg.skierowanie]++;
    } 
    else
    {
        stan->obs_spec[msg.typ_lekarza]++;
        stan->decyzja[msg.skierowanie]++;
    }
    semop(semid, &mutex_unlock, 1);
    
    if (potrzebny_rodzic && rodzic_utworzony) {
        pthread_cancel(rodzic_thread);
        pthread_join(rodzic_thread, NULL);
    }

    struct sembuf wyjscie_z_poczekalni = {SEM_MIEJSCA_SOR, sem_op_miejsca, SEM_UNDO};
    semop(semid, &wyjscie_z_poczekalni, 1);

    shmdt(stan);
    stan_ptr = NULL;

    return 0;
}
