#define _GNU_SOURCE
#include "wspolne.h"

int semid = -1;
int semid_limits = -1;
int msgid_stat = -1;
int shmid = -1;

int rej_msgid = -1;
int poz_id = -1;




volatile sig_atomic_t stan_pacjenta = STAN_PRZED_SOR;
int sem_op_miejsca = 1; // NAZWA JEST MYLĄCA - CHODZI O TO CZY TRAKTUJEMY PROCES JAKO JEDNA CZY DWIE OSOBY (DZIEKO + RODZIC) WAZNE PRZY EXITCIE
pthread_t rodzic_thread;
int RODZIC_POTRZEBNY = 0;   


typedef enum {
    BRAK_ZADAN,
    ZADANIE_WEJDZ_SEM,    
    ZADANIE_WYJDZ_SEM,    
    ZADANIE_REJESTRACJA,  
    ZADANIE_POZ,          
    ZADANIE_SPECJALISTA,  
    ZADANIE_KONIEC
} TypZadania;

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond_start; 
    pthread_cond_t cond_koniec;
    TypZadania aktualne_zadanie;
    KomunikatPacjenta dane_pacjenta; 
} OpiekunControlBlock;

OpiekunControlBlock OpiekunSync;




void aktualizuj_liczniki(int zmiana_przed, int zmiana_wew, int zmiana_kolejki_rej) {
    StanSOR *stan = (StanSOR*)shmat(shmid, NULL, 0);
    if (stan != (void*)-1) {
        struct sembuf lock = {SEM_DOSTEP_PAMIEC, -1, SEM_UNDO};
        struct sembuf unlock = {SEM_DOSTEP_PAMIEC, 1, SEM_UNDO};
        while(semop(semid, &lock, 1) == -1) { if (errno == EINTR) continue; return; }
        
        stan->pacjenci_przed_sor += zmiana_przed;
        stan->pacjenci_w_poczekalni += zmiana_wew;
        stan->dlugosc_kolejki_rejestracji += zmiana_kolejki_rej;

      
        
        int q = stan->dlugosc_kolejki_rejestracji;
        int cmd = stan->wymuszenie_otwarcia;
        if (q > PROG_OTWARCIA && cmd == 0) 
        {
        stan->wymuszenie_otwarcia = 1;
        zapisz_raport(RAPORT_1, semid, "[ Pacjent ] wymuszam otwarcie bramki numer 2\n");
        }
        else if (q < PROG_ZAMKNIECIA && cmd == 1) 
        {
        stan->wymuszenie_otwarcia = 0;
        zapisz_raport(RAPORT_1, semid, "[ Pacjent ] wymuszam zamkniecie bramki numer 2\n");
        }
        
        semop(semid, &unlock, 1);
        shmdt(stan);
    }
}

void lock_limit(int sem_indeks) {
    struct sembuf operacja = {sem_indeks, -1, SEM_UNDO};
    while (semop(semid_limits, &operacja, 1) == -1) if(errno != EINTR) break;
}
void unlock_limit(int sem_indeks) {
    struct sembuf operacja = {sem_indeks, 1, SEM_UNDO};
    semop(semid_limits, &operacja, 1);
}

void wykonaj_ipc_samodzielnie(int qid, int limit_id, KomunikatPacjenta *msg) {

    lock_limit(limit_id);
    while (msgsnd(qid, msg, sizeof(KomunikatPacjenta)-sizeof(long), 0) == -1) if(errno!=EINTR) break;   
    while (msgrcv(qid, msg, sizeof(KomunikatPacjenta)-sizeof(long), msg->pacjent_pid, 0) == -1) if(errno!=EINTR) break;
    unlock_limit(limit_id);
}

void zlec_opiekunowi(TypZadania zadanie) {
    pthread_mutex_lock(&OpiekunSync.mutex);
    OpiekunSync.aktualne_zadanie = zadanie;
    pthread_cond_signal(&OpiekunSync.cond_start);
    while (OpiekunSync.aktualne_zadanie != BRAK_ZADAN) {
        pthread_cond_wait(&OpiekunSync.cond_koniec, &OpiekunSync.mutex);
    }
    pthread_mutex_unlock(&OpiekunSync.mutex);
}

void* watek_opiekun(void* arg) {
    pid_t mpid = getpid();
    pthread_mutex_lock(&OpiekunSync.mutex);
    while (1) {
        while (OpiekunSync.aktualne_zadanie == BRAK_ZADAN) pthread_cond_wait(&OpiekunSync.cond_start, &OpiekunSync.mutex);
        if (OpiekunSync.aktualne_zadanie == ZADANIE_KONIEC) break;

        pthread_mutex_unlock(&OpiekunSync.mutex); 
        
        KomunikatPacjenta *msg = &OpiekunSync.dane_pacjenta;
        struct sembuf operacja_sem = {SEM_MIEJSCA_SOR, 0, SEM_UNDO}; 
        
        switch (OpiekunSync.aktualne_zadanie) {
            case ZADANIE_WEJDZ_SEM:
                operacja_sem.sem_op = -1;
                while (semop(semid, &operacja_sem, 1) == -1) if(errno!=EINTR) exit(104);
                aktualizuj_liczniki(-1, 1, 0); 
                break;
            case ZADANIE_WYJDZ_SEM:
                operacja_sem.sem_op = 1;
                semop(semid, &operacja_sem, 1);
                aktualizuj_liczniki(0, -1, 0);
                break;
            case ZADANIE_REJESTRACJA:
                wykonaj_ipc_samodzielnie(rej_msgid, SLIMIT_REJESTRACJA, msg);
                break;
            case ZADANIE_POZ:
                msg->mtype = 1;
                wykonaj_ipc_samodzielnie(poz_id, SLIMIT_POZ, msg);
                break;
            case ZADANIE_SPECJALISTA:
                if (msg->typ_lekarza > 0) {
                    int spec_id = msg->typ_lekarza;
                    int qid = msgget(ftok(FILE_KEY, (spec_id==1?'K':spec_id==2?'N':spec_id==3?'L':spec_id==4?'C':spec_id==5?'O':'D')), 0);
                    msg->mtype = msg->kolor;
                    wykonaj_ipc_samodzielnie(qid, spec_id + 1, msg);
                }
                break;
            default: break;
        }

        pthread_mutex_lock(&OpiekunSync.mutex);
        OpiekunSync.aktualne_zadanie = BRAK_ZADAN;
        pthread_cond_signal(&OpiekunSync.cond_koniec);
    }
    pthread_mutex_unlock(&OpiekunSync.mutex);
    return NULL;
}

void handle_kill(int sig) {
    if (RODZIC_POTRZEBNY) { 
    }
    if (stan_pacjenta == STAN_W_POCZEKALNI) _exit(sem_op_miejsca);
    _exit(0);
}


int main(int argc, char *argv[])
{
    signal(SIGINT, SIG_IGN);          
    signal(SIGTERM, handle_kill);     

    signal(SIGTSTP, SIG_DFL);
    signal(SIGCONT, SIG_DFL);

    srand(time(NULL) ^ getpid());
    stan_pacjenta = STAN_PRZED_SOR;

    semid = semget(ftok(FILE_KEY, ID_SEM_SET), 0, 0);
    semid_limits = semget(ftok(FILE_KEY, ID_SEM_LIMITS), 0, 0);
    shmid = shmget(ftok(FILE_KEY, ID_SHM_MEM), 0, 0);
    rej_msgid = msgget(ftok(FILE_KEY, ID_KOLEJKA_REJESTRACJA), 0);
    poz_id = msgget(ftok(FILE_KEY, ID_KOLEJKA_POZ), 0);
    msgid_stat = msgget(ftok(FILE_KEY, ID_KOLEJKA_STATYSTYKI), 0);

    if (semid == -1 || rej_msgid == -1 || shmid == -1) exit(102);

    pid_t mpid = getpid();
    int wiek = rand() % 100;
    int vip = rand() % 100 < 20;

    KomunikatPacjenta msg;
    memset(&msg, 0, sizeof(msg));
    msg.mtype = vip ? TYP_VIP : TYP_ZWYKLY;
    msg.wiek = wiek;
    msg.pacjent_pid = mpid;
    msg.czy_vip = vip;

    if (wiek < 18) {
        RODZIC_POTRZEBNY =1;
        sem_op_miejsca = 2;
        pthread_mutex_init(&OpiekunSync.mutex, NULL);
        pthread_cond_init(&OpiekunSync.cond_start, NULL);
        pthread_cond_init(&OpiekunSync.cond_koniec, NULL);
        OpiekunSync.aktualne_zadanie = BRAK_ZADAN;
        pthread_create(&rodzic_thread, NULL, watek_opiekun, NULL);
        
        zapisz_raport(KONSOLA, semid, "[PACJENT %d ] WIEK %d |Z DOROSLYM %lu |\n", mpid, wiek, (unsigned long)rodzic_thread);
    } else {
        sem_op_miejsca = 1;
        zapisz_raport(KONSOLA, semid, "[PACJENT  %d ] WIEK %d \n", mpid, wiek);
    }

 
    aktualizuj_liczniki(sem_op_miejsca, 0, 0); 

    if (RODZIC_POTRZEBNY) {
        zlec_opiekunowi(ZADANIE_WEJDZ_SEM);
    }

    struct sembuf wejscie = {SEM_MIEJSCA_SOR, -1, SEM_UNDO};
    while (semop(semid, &wejscie, 1) == -1) { if (errno == EINTR) continue; exit(104); }
    
    aktualizuj_liczniki(-1, 1, 1); 
    stan_pacjenta = STAN_W_POCZEKALNI;


    if (RODZIC_POTRZEBNY) {
        OpiekunSync.dane_pacjenta = msg; 
        zlec_opiekunowi(ZADANIE_REJESTRACJA);
        msg = OpiekunSync.dane_pacjenta;
    } else {
        wykonaj_ipc_samodzielnie(rej_msgid, SLIMIT_REJESTRACJA, &msg);
    }  
    


    aktualizuj_liczniki(0, 0, -1);

    if (RODZIC_POTRZEBNY) {
        OpiekunSync.dane_pacjenta = msg;
        zlec_opiekunowi(ZADANIE_POZ);
        msg = OpiekunSync.dane_pacjenta;
    } else {
        msg.mtype = 1; //w pdfie nie ma nic o priorytecie dla poz 
        wykonaj_ipc_samodzielnie(poz_id, SLIMIT_POZ, &msg);
    }



    if (msg.typ_lekarza > 0) {
        if (sem_op_miejsca == 2) {
            OpiekunSync.dane_pacjenta = msg;
            zlec_opiekunowi(ZADANIE_SPECJALISTA);
            msg = OpiekunSync.dane_pacjenta;
        } else {
            int spec_id = msg.typ_lekarza;
            int qid = msgget(ftok(FILE_KEY, (spec_id==1?'K':spec_id==2?'N':spec_id==3?'L':spec_id==4?'C':spec_id==5?'O':'D')), 0);
            msg.mtype = msg.kolor;
            wykonaj_ipc_samodzielnie(qid, spec_id + 1, &msg);
        }
    }


    stan_pacjenta = STAN_WYCHODZI; 
    
    StatystykaPacjenta stat;
    stat.mtype = 1;
    stat.czy_vip = vip;
    stat.kolor = msg.kolor;
    stat.typ_lekarza = msg.typ_lekarza;
    stat.skierowanie = msg.skierowanie;



    msgsnd(msgid_stat, &stat, sizeof(stat)-sizeof(long), 0);

   
    struct sembuf wyjscie = {SEM_MIEJSCA_SOR, 1, SEM_UNDO};
    semop(semid, &wyjscie, 1);

    aktualizuj_liczniki(0, -1, 0);

    if (RODZIC_POTRZEBNY) 
    {
        zlec_opiekunowi(ZADANIE_WYJDZ_SEM);
        
        pthread_mutex_lock(&OpiekunSync.mutex);
        OpiekunSync.aktualne_zadanie = ZADANIE_KONIEC;
        pthread_cond_signal(&OpiekunSync.cond_start);
        pthread_mutex_unlock(&OpiekunSync.mutex);
        pthread_join(rodzic_thread, NULL);

    }
    return 0;
}