#include "wspolne.h"
#include <stdio.h>
#include <errno.h>
#include <sys/wait.h>
#include <stdlib.h>

int semid = -1;
int shmid = -1;
int msgs_ids[20];
pid_t pid_lekarze[10];
//TO DO CZYSZCZENIE

pid_t uruchom_proces(const char* prog, const char* arg1, const char* kom)
{
    pid_t pid = fork();
    if (pid == 0)
    {
        signal(SIGINT, SIG_DFL);
        printf("tworzenie %s...\n", kom);
        execl(prog, "proc", arg1, NULL);
        perror("blad execl");
        exit(1);
    }

    return pid;
}



void signal_handler(int sig) 
{
    if (sig == SIGINT) {
       
        
        kill(0, SIG_EWAKUACJA); 
        
        printf("[MAIN] Czekam na zakończenie procesów potomnych...\n");
        while(wait(NULL) > 0); 
        
        czyszczenie();
        exit(0);
    }
}

int msg_creat(int index_w_tablicy, int klucz_char) 
{
    int id = msgget(ftok(FILE_KEY, klucz_char), IPC_CREAT | 0600);
    if (id == -1) perror("Blad msgget");
    msgs_ids[index_w_tablicy] = id;
    return id;
}


int main()
{
    signal(SIGINT, signal_handler);
    signal(SIG_EWAKUACJA, SIG_IGN);

    FILE *f = fopen(FILE_DEST, 'w');

    if (f)
    {
        fprintf(f, "\trozpoczynam symulacje...\n");
        fclose(f);
    }

    key_t key_shm = ftok(FILE_KEY, ID_SHM_MEM); 
    key_t key_sem = ftok(FILE_KEY, ID_SEM_SET);

    for(int i=0; i<20; i++) msgs_ids[i] = -1;

    msg_creat(0, ID_KOLEJKA_REJESTRACJA);
    msg_creat(1, ID_KOLEJKA_POZ);
    msg_creat(2, ID_KOLEJKA_WYNIKI);
    
    msg_creat(3, ID_KOL_KARDIOLOG);
    msg_creat(4, ID_KOL_NEUROLOG);
    msg_creat(5, ID_KOL_LARYNGOLOG);
    msg_creat(6, ID_KOL_CHIRURG);
    msg_creat(7, ID_KOL_OKULISTA);
    msg_creat(8, ID_KOL_PEDIATRA);

    shmid = shmget(key_shm, sizeof(StanSOR), IPC_CREAT | 0600);

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

    uruchom_proces("./rejestracja", "1", "REJESTRACJA 1");

    uruchom_proces("./rejestracja", "2", "REJESTRACJA 2");

    uruchom_proces("./lekarz", "0", "LEKARZ POZ");

    char buff[5]; 
    char buff_opis[50];
    for(int i=1; i<=6; i++) {
        sprintf(buff, "%d", i); 
        sprintf(buff_opis, "specjalista %d", i);
        pid_lekarze[i] = uruchom("./lekarz", buff, buff_opis);
    }

    uruchom_proces("./generator", NULL, "GENERATOR");
    
    int wybor;
    printf("\n\t menu dla uzytkownika\n");
    printf("0 - ewakuacja sor. analogicznie uzyj CTRL + C\n");
    printf("pozostale opcje: wezwij lekarza na oddzial:\n");
    printf("1 - pediatra\n2 - kardiolog\n3 - neurolog\n4 - okulista\n5 - laryngolog\n6 - chirurg");

    while(1)
    {
        printf("> ");
        if (scanf("%d", &wybor) != 1)
        {
            while(getchar() != '\n');
            continue;
        }

        if (wybor == 0)
        {
            raise(SIGINT);
        }
        else if (wybor >= 1 && wybor <= 6)
        {
            pid_t target = pid_lekarze[wybor];
            kill(target, SIG_LEKARZ_ODDZIAL);
        }
        else
        {
            printf("niepoprawne uzycie...\n");
        }
    }
   

    

    



    return 0;


}