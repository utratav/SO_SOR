#include "wspolne.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <string.h>


int main(int argc, char *argv[])
{

    key_t key_sem = ftok(FILE_KEY, ID_SEM_SET);
    key_t key_msg = ftok(FILE_KEY, ID_MSG_QUEUE);
    key_t key_shm = ftok(FILE_KEY, ID_SHM_MEM);

    int semid = semget(key_sem, 0, 0);
    int msgid = msgget(key_msg, 0);
    int shmid = shmget(key_shm, 0, 0);

    if (semid == -1 || msgid == -1 || shmid == -1)
    {
        perror("blad przy podlaczaniu do ipc");
        exit(EXIT_FAILURE);
    }

    StanSOR * stan = (StanSOR*)shmat(shmid, NULL, 0);

    if (stan == (void*)-1)
    {
        perror("blad shmat");
        exit(EXIT_FAILURE);
    }

    pid_t mpid = getpid();
    int wiek = rand() % 100;
    int vip = rand() % 100 < 20 //20% szans

    printf("pacjent %d --- wiek: %d --- vip: %s\n", mpid, wiek, vip ? "tak" : "nie");

    struct sembuf wejscie_do_poczekalni;
    wejscie_do_poczekalni.sem_num = SEM_MIEJSCA_SOR; 
    wejscie_do_poczekalni.sem_op = -1;
    wejscie_do_poczekalni.sem_flg = 0;


    if (semop(semid, &wejscie_do_poczekalni, 1) == -1)
    {
        perror("blad semop wejscie do poczekalni");
        exit(1);
    }

    struct sembuf mutex_lock;
    mutex_lock.sem_flg = 0;
    mutex_lock.sem_num = SEM_DOSTEP_PAMIEC;
    mutex_lock.sem_op = -1;


    struct sembuf mutex_unlock;
    mutex_lock.sem_flg = 0;
    mutex_lock.sem_num = SEM_DOSTEP_PAMIEC;
    mutex_lock.sem_op = 1;

    semop(semid, &mutex_lock, 1);
    stan->liczba_pacjentow_w_srodku++;
    stan->dlugosc_kolejki_rejestracji++;
    int aktualna_kolejka = stan->dlugosc_kolejki_rejestracji;
    semop(semid, &mutex_unlock, 1);

    if (aktualna_kolejka > LIMIT_KOLEJKI_K)
    {
        semop(semid, &mutex_lock, 1);
        if (stan->czy_okienko_2_otwarte == 0)
        {
            stan->czy_okienko_2_otwarte = 1;

            //ODPAL DRUGIE OKIENKO()
        }
        semop(semid, &mutex_unlock, 1);
    }

    KomunikatPacjenta msg;
    msg.mtype = 1;
    msg.pacjent_pid = mpid;

    msg.czy_vip = vip;
    sprintf(msg.opis_objawow, "objaw");

    if (msgsnd(&msg, sizeof(msg) - sizeof(long), 0) == -1)
    {
        perror("blad wysylania do rejestru");
    }



    sleep(10); // tutaj pacjent bedzie czekac na lekarza msgrcv()

    semop(semid, &mutex_lock, 1);
    stan->liczba_pacjentow_w_srodku--;
    semop(semid, &mutex_unlock, 1);
    shmdt(stan);

    struct sembuf wyjscie_z_poczekalni;
    wyjscie_z_poczekalni.sem_num = SEM_MIEJSCA_SOR; 
    wyjscie_z_poczekalni.sem_op = 1;
    wyjscie_z_poczekalni.sem_flg = 0;

    semop(semid, &wyjscie_z_poczekalni, 1);

    return 0;


}
