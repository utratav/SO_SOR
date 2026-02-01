
#define _GNU_SOURCE

#include "wspolne.h"
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <pthread.h>

int semid = -1;
int shmid = -1;
int semid_limits = -1;
int msgid_stat = -1;  // kolejka statystyk

volatile sig_atomic_t stan_pacjenta = STAN_PRZED_SOR;
volatile sig_atomic_t ewakuacja_flaga = 0;

int sem_op_miejsca = 1;
int potrzebny_rodzic = 0;
pthread_t rodzic_thread;
unsigned long id_opiekuna = 0;
volatile sig_atomic_t rodzic_utworzony = 0;


void handle_ewakuacja(int sig)
{
    if (stan_pacjenta > 0)
    {
        struct sembuf ewak;
        ewak.sem_flg = SEM_UNDO;
        ewak.sem_num = SEM_MIEJSCA_SOR;
        ewak.sem_op = sem_op_miejsca;

        semop(semid, &ewak, 1);
    }

    if (potrzebny_rodzic && rodzic_utworzony) {
        pthread_cancel(rodzic_thread);
        pthread_join(rodzic_thread, NULL);
    }

    if (stan != NULL && stan != (void*)-1) {
        shmdt(stan);
    }

    if (stan_pacjenta < 2) _exit(sem_op_miejsca);
}


void* watek_rodzic(void* arg)
{
    while(1) {
        usleep(1000);
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

int int_2_msgid(int typ) {
    switch(typ) {
        case LEK_POZ:        return ID_KOLEJKA_POZ;
        case LEK_KARDIOLOG:  return ID_KOL_KARDIOLOG;
        case LEK_NEUROLOG:   return ID_KOL_NEUROLOG;
        case LEK_LARYNGOLOG: return ID_KOL_LARYNGOLOG;
        case LEK_CHIRURG:    return ID_KOL_CHIRURG;
        case LEK_OKULISTA:   return ID_KOL_OKULISTA;
        case LEK_PEDIATRA:   return ID_KOL_PEDIATRA;
        default: return -1;
    }
}


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

    stan_pacjenta = STAN_PRZED_SOR;

    shmid = shmget(ftok(FILE_KEY, ID_SHM_MEM), 0, 0);
    StanSOR *stan = NULL;
    if (shmid != -1) {
        stan = (StanSOR*)shmat(shmid, NULL, 0);

    pid_t mpid = getpid();
    int wiek = rand() % 100;
    int vip = rand() % 100 < 20;

    potrzebny_rodzic = (wiek < 18);
    sem_op_miejsca = potrzebny_rodzic ? 2 : 1; 

    if (potrzebny_rodzic) 
    {
        if(pthread_create(&rodzic_thread, NULL, watek_rodzic, NULL) != 0) 
        {
            potrzebny_rodzic = 0; 
            sem_op_miejsca = 1;
        } 
        else 
        {           
            rodzic_utworzony = 1;
            id_opiekuna = (unsigned long)rodzic_thread;
        }
    }


    key_t key_sem = ftok(FILE_KEY, ID_SEM_SET);
    semid = semget(key_sem, 0, 0);

    char buf[150]; 
    if (potrzebny_rodzic)
        sprintf(buf, "[pacjent] id: %d --- wiek: %d --- vip: %s\t [rodzic] tid: %lu\n", 
                mpid, wiek, vip ? "tak" : "nie", id_opiekuna);
    else 
        sprintf(buf, "[pacjent] id: %d --- wiek: %d --- vip: %s\n", 
                mpid, wiek, vip ? "tak" : "nie");
    
    zapisz_raport(KONSOLA, semid, "%s", buf);

    
    key_t key_limits = ftok(FILE_KEY, ID_SEM_LIMITS);
    key_t key_msg_rej = ftok(FILE_KEY, ID_KOLEJKA_REJESTRACJA);
    key_t key_msg_poz = ftok(FILE_KEY, ID_KOLEJKA_POZ);
    key_t key_msg_stat = ftok(FILE_KEY, ID_KOLEJKA_STATYSTYKI);

    
    semid_limits = semget(key_limits, 0, 0);
    int rej_msgid = msgget(key_msg_rej, 0);
    int poz_id = msgget(key_msg_poz, 0);
    msgid_stat = msgget(key_msg_stat, 0);

    int spec_msgids[10];
    for (int i = 0; i < 10; i++) spec_msgids[i] = -1;
    spec_msgids[1] = msgget(ftok(FILE_KEY, ID_KOL_KARDIOLOG), 0);
    spec_msgids[2] = msgget(ftok(FILE_KEY, ID_KOL_NEUROLOG), 0);
    spec_msgids[3] = msgget(ftok(FILE_KEY, ID_KOL_LARYNGOLOG), 0);
    spec_msgids[4] = msgget(ftok(FILE_KEY, ID_KOL_CHIRURG), 0);
    spec_msgids[5] = msgget(ftok(FILE_KEY, ID_KOL_OKULISTA), 0);
    spec_msgids[6] = msgget(ftok(FILE_KEY, ID_KOL_PEDIATRA), 0);

    if (semid == -1 || rej_msgid == -1 || poz_id == -1 || semid_limits == -1 || msgid_stat == -1)
    {
        perror("blad przy podlaczaniu do ipc");
        exit(EXIT_FAILURE);
    }


    struct sembuf wejscie_do_poczekalni = {SEM_MIEJSCA_SOR, -sem_op_miejsca, SEM_UNDO};

    while (semop(semid, &wejscie_do_poczekalni, 1) == -1)
    {
        if (errno == EINTR) {
            usleep(10000);
            continue;
        }
        perror("blad semop wejscie do poczekalni");
        wykonaj_ewakuacje();
    }

    stan_pacjenta = STAN_W_POCZEKALNI;
    
    
    StanSOR *stan = NULL;
    if (shmid != -1) {
        stan = (StanSOR*)shmat(shmid, NULL, 0);
        if (stan != (void*)-1) 
        {
            struct sembuf lock = {SEM_DOSTEP_PAMIEC, -1, SEM_UNDO};
            struct sembuf unlock = {SEM_DOSTEP_PAMIEC, 1, SEM_UNDO};

            semop(semid, &lock, 1);
            stan->dlugosc_kolejki_rejestracji++;
            semop(semid, &unlock, 1);
        }
    }
    
    KomunikatPacjenta msg;
    memset(&msg, 0, sizeof(msg));
    msg.mtype = vip ? TYP_VIP : TYP_ZWYKLY;
    msg.wiek = wiek;
    msg.pacjent_pid = mpid;
    msg.czy_vip = vip;

    lock_limit(SLIMIT_REJESTRACJA);
    
    
    while (msgsnd(rej_msgid, &msg, sizeof(KomunikatPacjenta) - sizeof(long), 0) == -1)
    {
        if (errno == EINTR) { usleep(10000); continue; }
        if (errno == EAGAIN) { usleep(10000); continue; }
        perror("blad wysylania do rejestracji");
        unlock_limit(SLIMIT_REJESTRACJA);
        
    }    
 
    while (msgrcv(rej_msgid, &msg, sizeof(KomunikatPacjenta) - sizeof(long), mpid, 0) == -1)
    {
        if (errno == EINTR) { usleep(10000); continue; }
        perror("blad msgrcv od rejestracji");
        unlock_limit(SLIMIT_REJESTRACJA);
        
    }
    unlock_limit(SLIMIT_REJESTRACJA);

    if (stan != NULL && stan != (void*)-1) {
        struct sembuf lock = {SEM_DOSTEP_PAMIEC, -1, SEM_UNDO};
        struct sembuf unlock = {SEM_DOSTEP_PAMIEC, 1, SEM_UNDO};
        semop(semid, &lock, 1);
        if (stan->dlugosc_kolejki_rejestracji > 0)
            stan->dlugosc_kolejki_rejestracji--;
        semop(semid, &unlock, 1);
    }


   

    msg.mtype = 1; 
    lock_limit(SLIMIT_POZ);
    
    while (msgsnd(poz_id, &msg, sizeof(KomunikatPacjenta) - sizeof(long), 0) == -1)
    {
        if (errno == EINTR) { usleep(10000); continue; }
        if (errno == EAGAIN) { usleep(10000); continue; }
        perror("blad msgsnd wysylania do poz");
        unlock_limit(SLIMIT_POZ);
        
    }

    while (msgrcv(poz_id, &msg, sizeof(KomunikatPacjenta) - sizeof(long), mpid, 0) == -1)
    {
        if (errno == EINTR) { usleep(10000); continue; }
        perror("blad msgrcv od poz");
        unlock_limit(SLIMIT_POZ);
        
    }
    unlock_limit(SLIMIT_POZ);

    

    if (msg.typ_lekarza > 0)
    {
        int id_spec = msg.typ_lekarza;
        int spec_msgid = msgget(ftok(FILE_KEY, int_2_msgid(id_spec)), 0);

        
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
            

            while (msgsnd(spec_msgids[id_spec], &msg, sizeof(KomunikatPacjenta) - sizeof(long), 0) == -1)
            {
                if (errno == EINTR) { usleep(10000); continue; }
                if (errno == EAGAIN) { usleep(10000); continue; }
                perror("blad wysylania do specjalisty");
                unlock_limit(sem_limit_indeks);
                
            }

            while (msgrcv(spec_msgids[id_spec], &msg, sizeof(KomunikatPacjenta) - sizeof(long), mpid, 0) == -1)
            {
                if (errno == EINTR) { usleep(10000); continue; }
                perror("blad msgrcv od specjalisty");
                unlock_limit(sem_limit_indeks);
                
            }

            unlock_limit(sem_limit_indeks);
        }
    }


    StatystykaPacjenta stat_msg;
    stat_msg.mtype = 1;
    stat_msg.czy_vip = msg.czy_vip;
    stat_msg.kolor = msg.kolor;
    stat_msg.typ_lekarza = msg.typ_lekarza;
    stat_msg.skierowanie = msg.skierowanie;
    
    
    msgsnd(msgid_stat, &stat_msg, sizeof(StatystykaPacjenta) - sizeof(long), 0);

    signal(SIGINT, SIG_IGN);
    stan_pacjenta = STAN_WYCHODZI;
    
    if (potrzebny_rodzic && rodzic_utworzony) {
        pthread_cancel(rodzic_thread);
        pthread_join(rodzic_thread, NULL);
    }

    struct sembuf wyjscie_z_poczekalni = {SEM_MIEJSCA_SOR, sem_op_miejsca, SEM_UNDO};
    semop(semid, &wyjscie_z_poczekalni, 1);

    if (stan != NULL && stan != (void*)-1) {
        shmdt(stan);
    }

    _exit(0);
}
