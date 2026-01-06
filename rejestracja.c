


//  1. obluga dwoch okienek 



/*
    definiujemy z jakiego okienka korzystamy

    podlaczyc do wszystkich IPC

    pam_dziel - czy okienko dwa otwarte tak/nie

    sem dla pam_dziel

    kolejka wejsciowa - skad bierzemy pacjenta

    kolejka wyjsciowa czyli lekarz poz 



    int nr okienka =  argv[1]



    void handle_sig(nr okienka)


    shmid, semid,

    msgid_we = (id koelejka rejestracji)
    msgid_wy = (id kolejka poz)

    while 1:

    lock
    otwarte? = stan->czyokienkotwarte  
    dlugosc kol = stan-dlkol

    if(!otwarte?)
    sleep(1)
    continue


    if dlkolej < maxpacj / 3

    stan->czyokno2otwarte = 0
    unlock
    continue


    msgrcv(z recepcji)

    lock
    stan->koejka--
    unlock

    msgsnd(do lekarza poz)

    



*/


#include <wspolne.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

//kolejka kom odbieramy wiadomosc z pacjenta, wysylamy wiadomosc do poz

int nr_okienka;

void handle_sig(int sig)
{
    printf("rejestracja - koniec pracy okienka %d", nr_okienka);
    exit(0);
}

int main(int argc, char*argv[])
{

    signal(SIGTERM, handle_sig);

    if (argc < 2)
    {
        fprintf(stderr, "niepopawnie uzyta skladnia. uzyj: %s (int)nr_okienka", argv[0]);
        exit(1);
    }

    nr_okienka = atoi(argv[1]);

    int shmid = shmget(ftok(FILE_KEY, ID_SHM_MEM), 0, 0);
    int semid = semget(ftok(FILE_KEY, ID_SEM_SET), 0, 0);
    
    int msgid_we = msgget(ftok(FILE_KEY, ID_KOLEJKA_REJESTRACJA), 0);
    int msgid_wy = msgget(ftok(FILE_KEY, ID_KOLEJKA_POZ), 0);

    if (shmid == -1 || semid == -1 || msgid_we == -1 || msgid_wy == -1) {
        perror("rejestracja - Błąd połączenia IPC"); exit(1);
    }

    StanSOR *stan = (StanSOR*)shmat(shmid, NULL, 0);

    struct sembuf lock;
    lock.sem_flg = 0;
    lock.sem_num = SEM_DOSTEP_PAMIEC;
    lock.sem_op = -1;

    struct sembuf unlock;
    unlock.sem_flg = 0;
    unlock.sem_num = SEM_DOSTEP_PAMIEC;
    unlock.sem_op = 1;

    KomunikatPacjenta pacjent;

    while(1)
    {
        if (nr_okienka == 2)
        {
            semop(semid, &lock, 1);
            int czy_otwarte = stan->czy_okienko_2_otwarte;
            int dlugosc_kolejki = stan->dlugosc_kolejki_rejestracji;
            semop(semid, &unlock, 1);

            if (!czy_otwarte)
            {
                sleep(1);
                continue;
            }

            if(dlugosc_kolejki < (MAX_PACJENTOW / 3))
            {
                semop(semid, &lock, 1);
                stan->czy_okienko_2_otwarte = 0;
                semop(semid, &unlock, 1);
                continue;   
            }

           
        }

        ssize_t status = msgrcv(msgid_we, &pacjent, sizeof(pacjent) - sizeof(long), -2, 0);

            if (status == -1)
            {
                if (errno != EINTR)
                {
                    perror("rejestracja - blad msgrcv");
                }
            }


        printf("rejestracja %d- obsluguje pacjenta %d, ktory %s VIPEM o objawach: %s",
                nr_okienka, pacjent.pacjent_pid, 
                (pacjent.mtype == 1) ? "JEST" : "NIE JEST", pacjent.opis_objawow);

        usleep(500000);

        semop(semid, &lock, 1);
        stan->dlugosc_kolejki_rejestracji--;
        semop(semid, &unlock, 1);

        pacjent.mtype = 1; //nie jestem tego pewien zobacz pozniej

        if(msgsnd(msgid_wy, &pacjent, sizeof(pacjent) - sizeof(long), 0) == -1)
        {
            perror("rejestracja - blad przekazania do POZ");
        }
        else
        {
            printf("rejestracja %d - pacjent %d przekazany do lekarza POZ",nr_okienka, pacjent.pacjent_pid);
        }


        

    }
    

    return 0;
}


