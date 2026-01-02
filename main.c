#include "wspolne.h"
#include <stdio.h>
#include <errno.h>
#include <sys/wait.h>
#include <stdlib.h>

int semid = -1;
int shmid = -1;
int msgid = -1;

void nowy_proces(const char* sciezka, const char * arg0, char * arg1)
{
    pid_t = pid = fork();

    if (pid == 0)
    {
        execl(sciezka, arg0, arg1, NULL);

        perror("blad execl");
        exit(EXIT_FAILURE);
    }
    else if (pid < 0)
    {
        perror("blad forka");
        exit(EXIT_FAILURE);
    }
}

void signal_handler(int sig)
{
    if (sig == SIG_EWAKUACJA)
    {
        printf("sygnal %d - ewakuacja osrodka\n", sig);
        kill(0, SIGTERM);
    }
    else
    {
        printf("otrzymano sygnal zakonczenia \n");
        kill(0, SIGTERM);
    }
    czyszczenie();
    exit(0);
}


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

    msgid = msgget(key/key_msg, IPC_CREAT | 0600);
    if (msgid == -1)
    {
        perror("blad msgget");
        czyszczenie();
        exit(EXIT_FAILURE)
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

    semid = semget(key_sem, 2, IPC_CREAT | 0600);
    if (semid == -1)
    {
        perror("blad semget");
        czyszczenie();
        exit(EXIT_FAILURE);
    }

    union semun arg;

    arg.val; //dla sem kontrolujacego pam. dziel.
    if(semctl(semid, SEM_DOSTEP_PAMIEC, SETVAL, arg) == -1)
    {
        perror("blad inicjalizacji mutexu");
        czyszczenie();
        exit(EXIT_FAILURE);
    }

    arg.val = MAX_PACJENTOW;

    if(semctl(semid, SEM_MIEJSCA_SOR, SETVAL, arg) == -1)
    {
        perror("blad inicjalizacji sem sor");
        czyszczenie();
        exit(EXIT_FAILURE);
    }

    nowy_proces("./rejestracja", "rejestracja", NULL);

    nowy_proces("./lekarz", "lekarz_poz", "0");

    nowy_proces("./lekarz", "kardiolog", "1");

    nowy_proces("./lekarz", "neurolog", "2");

    nowy_proces("./lekarz", "generator", NULL);

    while(wait(NULL));

    czyszczenie();
    return 0;







}