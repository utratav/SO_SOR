#include "wspolne.h"
#include <stdio.h>
#include <errno.h>
#include <sys/wait.h>
#include <stdlib.h>


int main()
{
    key_t key_msg = ftok(FILE_KEY, ID_MSG_QUEUE);
    key_t key_shm = ftok(FILE_KEY, ID_SHM_MEM);
    key_t key_sem = ftok(FILE_KEY, ID_SEM_SET);

    key_t klucze[3];
    klucze[0] = key_msg;
    klucze[1] = key_shm;
    klucze[2] = key_sem;

    for (int i = 0; i < 3; i++)
    {
        if (klucze[i] == -1)
        {
            perror("blad klucza %d", i);
            exit(1);
        }
    }

    shmid = shmget(key_shm, sizeof(StanSOR), IPC_CREAT | 0600);

    if (shmid == -1)
    {
        perror("blad shmget");
        czyszczenie();
        exit(EXIT_FAILURE);
    }

    StanSOR * stan = (StanSOR*)shmat(shmid, NULL, 0);

    if (stan == (void*)-1)
    {
        perror("blad shmat");
        czysczenie();
        exit(EXIT_FAILURE);
    }

    stan->liczba_pacjentow_w_srodku = 0;
    stan ->dlugosc_kolejki_rejestracji = 0;
    stan->czy_okienko_2_otwarte = 0; //flaga
    shmdt(stan);

    

}