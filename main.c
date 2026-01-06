#include "wspolne.h"
#include <stdio.h>
#include <errno.h>
#include <sys/wait.h>
#include <stdlib.h>

int semid = -1;
int shmid = -1;
int msgid_rej = -1;
int msgid_poz = -1;
int msgid_neuro = -1;
int msgid_kardio = -1;
int msgid_wynik = -1;
int msgid_ped = -1;
//TO DO CZYSZCZENIE

void nowy_proces(const char* sciezka, const char * arg0, char * arg1)
{
    pid_t pid = fork();

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
    key_t key_shm = ftok(FILE_KEY, ID_SHM_MEM);
    key_t key_sem = ftok(FILE_KEY, ID_SEM_SET);

    key_t key_msg_rej = ftok(FILE_KEY, ID_KOLEJKA_REJESTRACJA);
    key_t key_msg_poz = ftok(FILE_KEY, ID_KOLEJKA_POZ);
    key_t key_msg_kardio = ftok(FILE_KEY, ID_KOLEJKA_KARDIOLOG);
    key_t key_msg_neuro = ftok(FILE_KEY, ID_KOLEJKA_NEUROLOG);
    key_t key_msg_wyn = ftok(FILE_KEY, ID_KOLEJKA_WYNIKI);
    key_t key_msg_ped = ftok(FILE_KEY, ID_KOLEJKA_PEDIATRA);

    key_t klucze[8];

    klucze[0] = key_shm;
    klucze[1] = key_sem;
    klucze[2] = key_msg_rej;
    klucze[3] = key_msg_poz;
    klucze[4] = key_msg_kardio;
    klucze[5] = key_msg_neuro;
    klucze[6] = key_msg_wyn;
    klucze[7] = key_msg_ped;

    for (int i = 0; i < 8; i++)
    {
        if (klucze[i] == -1)
        {
            perror("blad klucza");
            exit(1);
        }
    }

    msgid_rej = msgget(msgid_rej, IPC_CREAT | 0600);
    msgid_poz = msgget(msgid_poz, IPC_CREAT | 0600);
    msgid_kardio = msgget(msgid_kardio, IPC_CREAT | 0600);
    msgid_neuro = msgget(msgid_neuro, IPC_CREAT | 0600);
    msgid_wynik = msgget(key_msg_wyn, IPC_CREAT | 0600);
    msgid_ped = msgget(key_msg_ped, IPC_CREAT | 0600);

    if (msgid_rej == -1 || msgid_poz == 1)
    {
        perror("Blad tworzenia kolejek dla rejestracji/poz");
        exit(1);
    }
    
    
    if (msgid_kardio == -1 || msgid_neuro == 1)
    {
        perror("Blad tworzenia kolejek dla specjalistow"); 
        exit(1);
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

    arg.val = 1; //dla sem kontrolujacego pam. dziel.
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

    while(wait(NULL) > 0);

    czyszczenie();



    return 0;


}