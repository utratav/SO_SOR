#include "wspolne.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/sem.h>

int semid = -1;

void handle_sig(sig)
{
    if (sig == SIG_EWAKUACJA)
    {
        char buf[50];
        sprintf(buf, "[pacjent %d] ewakuacja", getpid());
        zapisz_raport(FILE_DEST, semid, buf);
        exit(0);
    }
}

int main(int argc, char *argv[])
{
    signal(SIG_EWAKUACJA, handle_sig); 

    srand(time(NULL) ^ getpid());

    key_t key_sem = ftok(FILE_KEY, ID_SEM_SET);

    key_t key_msg_rej = ftok(FILE_KEY, ID_KOLEJKA_REJESTRACJA);
    key_t key_msg_wyn = ftok(FILE_KEY, ID_KOLEJKA_WYNIKI);
    key_t key_msg_poz = ftok(FILE_KEY, ID_KOLEJKA_POZ);

    key_t key_shm = ftok(FILE_KEY, ID_SHM_MEM);

    semid = semget(key_sem, 0, 0);

    int rej_msgid = msgget(key_msg_rej, 0);
    int wynik_id = msgget(key_msg_wyn, 0);
    int poz_id = msgget(key_msg_poz, 0);

    int shmid = shmget(key_shm, 0, 0);

    int msgid_spec[10];

    int spec_msgids[10];
    spec_msgids[1] = msgget(ftok(FILE_KEY, ID_KOL_KARDIOLOG), 0);
    spec_msgids[2] = msgget(ftok(FILE_KEY, ID_KOL_NEUROLOG), 0);
    spec_msgids[3] = msgget(ftok(FILE_KEY, ID_KOL_LARYNGOLOG), 0);
    spec_msgids[4] = msgget(ftok(FILE_KEY, ID_KOL_CHIRURG), 0);
    spec_msgids[5] = msgget(ftok(FILE_KEY, ID_KOL_OKULISTA), 0);
    spec_msgids[6] = msgget(ftok(FILE_KEY, ID_KOL_PEDIATRA), 0);

    if (semid == -1 || rej_msgid == -1 || shmid == -1 || wynik_id == -1 || poz_id == -1)
    {
        perror("blad przy podlaczaniu do ipc");
        exit(EXIT_FAILURE);
    }

    int err = 0;
    for (int i = 0; i < 10; i++)
    {
        if (spec_msgids[i] == -1) err = i;
       
        continue;
    }

    if (err > 0) {perror("blad msgid dla splecjalisty"); exit(EXIT_FAILURE);}

    StanSOR * stan = (StanSOR*)shmat(shmid, NULL, 0);

    if (stan == (void*)-1)
    {
        perror("blad shmat");
        exit(EXIT_FAILURE);
    }

    pid_t mpid = getpid();
    int wiek = rand() % 100;
    int vip = rand() % 100 < 20; //20% szans

    char buf[80];
    sprintf(buf, "[pacjent] id: %d --- wiek: %d --- vip: %s\n", mpid, wiek, vip ? "tak" : "nie");
    zapisz_raport(FILE_DEST, semid, buf);

    struct sembuf wejscie_do_poczekalni;
    wejscie_do_poczekalni.sem_num = SEM_MIEJSCA_SOR; 
    wejscie_do_poczekalni.sem_op = -1;
    wejscie_do_poczekalni.sem_flg = SEM_UNDO;


    if (semop(semid, &wejscie_do_poczekalni, 1) == -1)
    {
        perror("blad semop wejscie do poczekalni");
        exit(1);
    }

    struct sembuf mutex_lock;
    mutex_lock.sem_flg = SEM_UNDO;
    mutex_lock.sem_num = SEM_DOSTEP_PAMIEC;
    mutex_lock.sem_op = -1;


    struct sembuf mutex_unlock;
    mutex_lock.sem_flg = SEM_UNDO;
    mutex_lock.sem_num = SEM_DOSTEP_PAMIEC;
    mutex_lock.sem_op = 1; 

    semop(semid, &mutex_lock, 1);

    stan->liczba_pacjentow_w_srodku++;
    stan->dlugosc_kolejki_rejestracji++;      

    semop(semid, &mutex_unlock, 1);
    

    KomunikatPacjenta msg;
    msg.mtype = vip ? TYP_VIP : TYP_ZWYKLY;
    msg.wiek = wiek;
    msg.pacjent_pid = mpid;

    msg.czy_vip = vip;
    

    if (msgsnd(rej_msgid, &msg, sizeof(msg) - sizeof(long), 0) == -1)
    {
        perror("blad wysylania do rejestracji");
    }   
 

    if (msgrcv(wynik_id, &msg, sizeof(msg) - sizeof(long), mpid, 0) == -1)
    {
        perror("blad msgrcv od rejestracji");
        exit(1);
    }

    msg.mtype = 1; //wyrownujemy vip i zwyklych do tego samego prior. w msgrcv i tak msgtype = 0
    if (msgsnd(poz_id, &msg, sizeof(msg) - sizeof(long), 0) == -1)
    {
        perror("blad msgsnd wysylania do poz");
    }

    if (msgrcv(wynik_id, &msg, sizeof(msg) - sizeof(long), mpid, 0) == -1)
    {
        perror("blad msgrcv od poz");
        exit(1);
    }

    if (msg.typ_lekarza > 0)
    {
        int id_spec = msg.typ_lekarza;
        msg.mtype = msg.kolor;

        if (msgsnd(spec_msgids[id_spec], &msg, sizeof(msg) - sizeof(long), 0) == -1)
        {
            perror("blad wysylania do specjalisty");
        }

        if (msgrcv(wynik_id, &msg, sizeof(msg) - sizeof(long), mpid, 0) == -1)
        {
            perror("blad msgrcv od specjalisty");
        }
    }


    semop(semid, &mutex_lock, 1);
    stan->liczba_pacjentow_w_srodku--;
    semop(semid, &mutex_unlock, 1);
    

    struct sembuf wyjscie_z_poczekalni;
    wyjscie_z_poczekalni.sem_num = SEM_MIEJSCA_SOR; 
    wyjscie_z_poczekalni.sem_op = 1;
    wyjscie_z_poczekalni.sem_flg = SEM_UNDO;

    semop(semid, &wyjscie_z_poczekalni, 1);
    shmdt(stan);

    return 0;




}
 