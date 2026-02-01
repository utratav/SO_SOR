#define _GNU_SOURCE
#include "wspolne.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

int nr_okienka;
int semid = -1;
volatile sig_atomic_t koniec_pracy = 0;

void handle_sig(int sig)
{
    if (sig == SIGTERM)
    {
        zapisz_raport(KONSOLA, semid, "[rejestracja %d] Otrzymano sygnal zakonczenia. Zamykam okienko.\n", nr_okienka);
        koniec_pracy = 1;
    }
}

int main(int argc, char*argv[]) 
{
    // Obsługa SIGTERM
    struct sigaction sa;
    sa.sa_handler = handle_sig;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    
    signal(SIGINT, SIG_IGN);
    
    signal(SIGTSTP, SIG_DFL);
    signal(SIGCONT, SIG_DFL);

    if (argc < 2)
    {
        fprintf(stderr, "niepopawnie uzyta skladnia. uzyj: %s (int)nr_okienka\n", argv[0]);
        exit(1);
    }

    nr_okienka = atoi(argv[1]);

    int shmid = shmget(ftok(FILE_KEY, ID_SHM_MEM), 0, 0);
    semid = semget(ftok(FILE_KEY, ID_SEM_SET), 0, 0);
    
    int msgid_we = msgget(ftok(FILE_KEY, ID_KOLEJKA_REJESTRACJA), 0);

    if (shmid == -1 || semid == -1 || msgid_we == -1) {
        perror("rejestracja - Błąd połączenia IPC"); 
        exit(1);
    }

    StanSOR *stan = (StanSOR*)shmat(shmid, NULL, 0);

    struct sembuf lock = {SEM_DOSTEP_PAMIEC, -1, SEM_UNDO};
    struct sembuf unlock = {SEM_DOSTEP_PAMIEC, 1, SEM_UNDO};

    struct sembuf czekaj_na_prace = {SEM_BRAMKA_2, -1, 0}; 
    struct sembuf oddaj_prace = {SEM_BRAMKA_2, 1, 0}; 

    KomunikatPacjenta pacjent;
    int lokalnie_otwarte = 0;

    while(!koniec_pracy)
    {
        
        
        if (nr_okienka == 1)
        {
            semop(semid, &lock, 1); 
            
            int wymagane_otwarcie = stan->czy_okienko_2_otwarte;
            int dlugosc_kolejki = stan->dlugosc_kolejki_rejestracji;

            if (wymagane_otwarcie && !lokalnie_otwarte)
            {
                lokalnie_otwarte = 1;
                semop(semid, &oddaj_prace, 1); 
                zapisz_raport(KONSOLA, semid, "[REJESTRACJA] Wykryto flage od pacjenta -> Otwieram okienko 2.\n");
            }

            if (lokalnie_otwarte && dlugosc_kolejki <= (MAX_PACJENTOW / 3)) 
            {
                stan->czy_okienko_2_otwarte = 0;
                lokalnie_otwarte = 0;

                semop(semid, &czekaj_na_prace, 1); 

                zapisz_raport(KONSOLA, semid, "[REJESTRACJA] Zamykam okienko 2 (Kolejka: %d)\n", dlugosc_kolejki);
                zapisz_raport(RAPORT_2, semid, "[REJESTRACJA] Zamykam 2 okienko | osob w kolejce: %d\n", dlugosc_kolejki);
            }
            
            semop(semid, &unlock, 1); 
        }

        if (nr_okienka == 2)
        {
            struct sembuf czekaj_nowait = {SEM_BRAMKA_2, -1, IPC_NOWAIT};
            if (semop(semid, &czekaj_nowait, 1) == -1) {
                if (errno == EAGAIN) {
                    usleep(100000);
                    continue;
                }
                if (errno == EINTR) continue;
                perror("rejestracja 2 - blad semop (czekaj)");
                exit(1);
            }
        }

        int flaga = IPC_NOWAIT; 
        
        ssize_t status = msgrcv(msgid_we, &pacjent, sizeof(KomunikatPacjenta) - sizeof(long), -2, flaga);

        if (status == -1)
        {
            if (errno == ENOMSG) {
                if (nr_okienka == 2) semop(semid, &oddaj_prace, 1);
                usleep(100000);
                continue;
            }
            
            if (errno == EINTR) {
                if (nr_okienka == 2) semop(semid, &oddaj_prace, 1);
                continue;
            }
            
            perror("rejestracja - blad msgrcv");
            if (nr_okienka == 2) semop(semid, &oddaj_prace, 1);
            continue;
        }

        pacjent.mtype = pacjent.pacjent_pid; 
        if(msgsnd(msgid_we, &pacjent, sizeof(KomunikatPacjenta) - sizeof(long), 0) == -1)
        {
            if (errno != EINTR)
                perror("rejestracja - blad przekazania do POZ");
        }
        else
        {
            zapisz_raport(KONSOLA, semid, "[rejestracja] okienko %d: pacjent %d przekazany do POZ\n",
                nr_okienka, pacjent.pacjent_pid);
        }        

        if (nr_okienka == 2) semop(semid, &oddaj_prace, 1);
    }
    
    shmdt(stan);
    return 0;
}
