#include "wspolne.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>


int nr_okienka;

int semid = -1;

void handle_sig(int sig)
{
    char buf[80];
    sprintf(buf, "[rejestracja] sygnal ewakuacja - koniec pracy okienka %d\n", nr_okienka);
    zapisz_raport(FILE_DEST, semid, buf);
    exit(0);
}

int main(int argc, char*argv[]) 
{

    signal(SIGTERM, handle_sig);

    if (argc < 2)
    {
        fprintf(stderr, "niepopawnie uzyta skladnia. uzyj: %s (int)nr_okienka\n", argv[0]);
        exit(1);
    }

    nr_okienka = atoi(argv[1]);

    int shmid = shmget(ftok(FILE_KEY, ID_SHM_MEM), 0, 0);
    semid = semget(ftok(FILE_KEY, ID_SEM_SET), 0, 0);
    
    int msgid_we = msgget(ftok(FILE_KEY, ID_KOLEJKA_REJESTRACJA), 0);
    int msgid_wy = msgget(ftok(FILE_KEY, ID_KOLEJKA_WYNIKI), 0);

    if (shmid == -1 || semid == -1 || msgid_we == -1 || msgid_wy == -1) {
        perror("rejestracja - Błąd połączenia IPC"); exit(1);
    }

    StanSOR *stan = (StanSOR*)shmat(shmid, NULL, 0);

    struct sembuf lock;
    lock.sem_flg = SEM_UNDO;
    lock.sem_num = SEM_DOSTEP_PAMIEC;
    lock.sem_op = -1;

    struct sembuf unlock;
    unlock.sem_flg = SEM_UNDO;
    unlock.sem_num = SEM_DOSTEP_PAMIEC;
    unlock.sem_op = 1;

    KomunikatPacjenta pacjent;

    while(1)
    {
        if (nr_okienka == 2)
        {
            semop(semid, &lock, 1);
            int kolejka = stan->dlugosc_kolejki_rejestracji;
            int otwarte = stan->czy_okienko_2_otwarte;
            
            if (!otwarte && kolejka > (MAX_PACJENTOW / 2)) 
            {
                stan->czy_okienko_2_otwarte = 1;
                char buf[60];
                sprintf(buf, "[rejestracja] otwieranie okienka 2, osob w kolejce: %d\n", kolejka);
                zapisz_raport(FILE_DEST, semid, buf);
                otwarte = 1;
            }
            else if (otwarte && kolejka < (MAX_PACJENTOW / 3)) 
            {
                stan->czy_okienko_2_otwarte = 0;
                char buf[60];
                sprintf(buf, "[rejestracja] zamykanie okienka 2, osob w kolejce: %d\n", kolejka);
                zapisz_raport(FILE_DEST, semid, buf);
                otwarte = 0;
            }
            semop(semid, &unlock, 1);

            
            if (!otwarte) {
                usleep(200000); // 0.2 sekundy
                continue;
            }
        }

        int flaga = 0;
        if (nr_okienka == 2) flaga = IPC_NOWAIT;

        ssize_t status = msgrcv(msgid_we, &pacjent, sizeof(pacjent) - sizeof(long), -2, flaga);

        if (status == -1)
        {
            if (errno == ENOMSG && nr_okienka == 2) {
                // Brak wiadomości, okienko 2 może sprawdzić czy się zamknąć
                usleep(100000);
                continue;
            }
            if (errno != EINTR) perror("rejestracja - blad msgrcv");
            continue;
        }


        

        usleep(500000);  

        semop(semid, &lock, 1);
        stan->dlugosc_kolejki_rejestracji--;
        semop(semid, &unlock, 1);

        pacjent.mtype = pacjent.pacjent_pid; 
        if(msgsnd(msgid_wy, &pacjent, sizeof(pacjent) - sizeof(long), 0) == -1)
        {
            perror("rejestracja - blad przekazania do POZ");
        }
        else
        {
            char buf[60];
            sprintf(buf, "[rejestracja] okienko %d: pacjent %d przekazany do POZ\n",
                 nr_okienka, pacjent.pacjent_pid);
            zapisz_raport(FILE_DEST, semid, buf);

        }        

    }
    

    return 0;
}


