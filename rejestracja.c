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
    if (sig == SIG_EWAKUACJA)
    {
        
        zapisz_raport(KONSOLA, semid, "[rejestracja %d] Otrzymano ewakuacje. Zamykam okienko .\n", nr_okienka);
        
        exit(0); 
    }
}



int main(int argc, char*argv[]) 
{

    signal(SIG_EWAKUACJA, handle_sig);

    if (argc < 2)
    {
        fprintf(stderr, "niepopawnie uzyta skladnia. uzyj: %s (int)nr_okienka\n", argv[0]);
        exit(1);
    }

    nr_okienka = atoi(argv[1]);

    int shmid = shmget(ftok(FILE_KEY, ID_SHM_MEM), 0, 0);
    semid = semget(ftok(FILE_KEY, ID_SEM_SET), 0, 0);
    
    int msgid_we = msgget(ftok(FILE_KEY, ID_KOLEJKA_REJESTRACJA), 0);
    

    if (shmid == -1 || semid == -1 || msgid_we == -1 ) {
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
        if (nr_okienka == 1)
        {
            semop(semid, &lock, 1);
            int kolejka = stan->dlugosc_kolejki_rejestracji;
            int otwarte = stan->czy_okienko_2_otwarte;

            if (otwarte && kolejka <= (MAX_PACJENTOW / 3)) 
            {
                stan->czy_okienko_2_otwarte = 0;
                zapisz_raport(KONSOLA, semid, "[REJESTRACJA] Zamykam okienko 2. (Kolejka: %d)\n", kolejka);
                zapisz_raport(RAPORT_2, semid, "[REJESTRACJA] Zamykam 2 okienko | osob w kolejce: %d\n", kolejka);
            }
            semop(semid, &unlock, 1);
        }

        // ---------------------------------------------------------
        // ROLA PRACOWNIKA DODATKOWEGO (Tylko Okienko 2)
        // ---------------------------------------------------------
        if (nr_okienka == 2)
        {
            // Sprawdzamy czy mamy pozwolenie na prace
            semop(semid, &lock, 1);
            int czy_pracowac = stan->czy_okienko_2_otwarte;
            semop(semid, &unlock, 1);

            if (!czy_pracowac)
            {
                usleep(50000); // Spij 50ms i sprawdz znowu
                continue;      // Wroc na poczatek petli
            }
        }

        int flaga = 0;
        if (nr_okienka == 2) flaga = IPC_NOWAIT;

        
        ssize_t status = msgrcv(msgid_we, &pacjent, sizeof(KomunikatPacjenta) - sizeof(long), -2, flaga);

        if (status == -1)
        {
            if (errno == ENOMSG && nr_okienka == 2) {
                
                usleep(100000);
                continue;
            }
            if (errno != EINTR) perror("rejestracja - blad msgrcv");
            continue;
        }


        

        //usleep(500000);  

        

        pacjent.mtype = pacjent.pacjent_pid; 
        if(msgsnd(msgid_we, &pacjent, sizeof(KomunikatPacjenta) - sizeof(long), 0) == -1)
        {
            perror("rejestracja - blad przekazania do POZ");
        }
        else
        {
                zapisz_raport(KONSOLA, semid, "[rejestracja] okienko %d: pacjent %d przekazany do POZ\n",
                nr_okienka, pacjent.pacjent_pid);

        }        

    }
    

    return 0;
}


