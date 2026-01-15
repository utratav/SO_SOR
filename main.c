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

void czyszczenie() 
{     
    if (shmid != -1) {
        StanSOR *stan = (StanSOR*)shmat(shmid, NULL, 0);
        if (stan != (void*)-1) 
        {
            podsumowanie(stan);
            shmdt(stan);
        }
    }

    if (shmid != -1) {
        if (shmctl(shmid, IPC_RMID, NULL) == -1) perror("Blad usuwania SHM");
        else zapisz_raport(FILE_DEST, semid, "[main] usunieto pamiec dzielona");;
    }

    if (semid != -1) {
        if (semctl(semid, 0, IPC_RMID) == -1) perror("Blad usuwania SEM");
        else zapisz_raport(FILE_DEST, semid, "[main] usunieto semafory");
    }

    for (int i = 0; i < 20; i++) {
        if (msgs_ids[i] != -1) {
            if (msgctl(msgs_ids[i], IPC_RMID, NULL) == -1) {
                // Ignorujemy blad jesli kolejka juz nie istnieje
                if (errno != EINVAL) perror("Blad usuwania kolejki");
            }
        }
    }
    zapisz_raport(FILE_DEST, semid, "[main] usunieto kolejki komunikatow");
    zapisz_raport(FILE_DEST, semid, "[main] wykonano czyszczenie");
}

pid_t uruchom_proces(const char* prog, const char* arg1)
{
    pid_t pid = fork();
    if (pid == 0)
    {
        
        if(arg1) execl(prog, "proc", arg1, NULL);
        else execl(prog, "proc", NULL);
        
        perror("blad execl");
        exit(1);
    }
    return pid;
}



void signal_handler(int sig) 
{
    if (sig == SIGINT) {
       
        zapisz_raport(FILE_DEST, semid, "[main] otrzymano sygnal ewakuacja...\n");
        kill(0, SIG_EWAKUACJA); 
        zapisz_raport(FILE_DEST, semid, "[main] zamykam procesy potomne...\n");
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

    FILE *f = fopen(FILE_DEST, "w");

    if (f)
    {
        fprintf(f, "[main]\trozpoczynam symulacje...\n");
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
    if (shmid == -1)
    {
        perror("blad shmat");
        exit(EXIT_FAILURE);
    }

    StanSOR * stan = (StanSOR*)shmat(shmid, NULL, 0);

    if (stan == (void*)-1)
    {
        perror("blad shmat");
        czyszczenie();
        exit(EXIT_FAILURE);
    }

    stan->liczba_pacjentow_w_srodku = 0;
    stan->dlugosc_kolejki_rejestracji = 0;
    stan->czy_okienko_2_otwarte = 0;
    stan->obs_pacjenci = 0;
    stan->obs_dom_poz = 0;

    for(int i=0; i<7; i++) stan->obs_spec[i] = 0;
    for(int i=0; i<4; i++) stan->obs_kolory[i] = 0;
    for(int i=0; i<4; i++) stan->decyzja[i] = 0; 
    shmdt(stan); 

    semid = semget(key_sem, 4, IPC_CREAT | 0600);
    if (semid == -1)
    {
        perror("blad semget");
        czyszczenie();
        exit(EXIT_FAILURE);
    }
 
    union semun arg;  

    arg.val = 1; 
    if(semctl(semid, SEM_DOSTEP_PAMIEC, SETVAL, arg) == -1)
    {
        perror("blad inicjalizacji sem pam dzielona");
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

    arg.val = 1; 
    if(semctl(semid, SEM_ZAPIS_PLIK, SETVAL, arg) == -1)
    {
        perror("blad inicjalizacji sem zapis do pliku");
        czyszczenie();
        exit(EXIT_FAILURE);
    }

    arg.val = MAX_PROCESOW;
    if(semctl(semid, SEM_GENERATOR, SETVAL, arg) == -1)
    {
        perror("blad inicjalizacji sem generacja procesow pacjent");
        czyszczenie();
        exit(EXIT_FAILURE);
    }



    uruchom_proces("./rejestracja", "1");

    uruchom_proces("./rejestracja", "2");

    uruchom_proces("./lekarz", "0");

    char buff[5]; 
    
    for(int i=1; i<=6; i++) {
        sprintf(buff, "%d", i); 
        pid_lekarze[i] = uruchom_proces("./lekarz", buff);
    }

    uruchom_proces("./generator", NULL);
    
    int wybor;
    printf("\n\t menu dla uzytkownika\n");
    printf("0 - ewakuacja sor. analogicznie uzyj CTRL + C\n");
    printf("pozostale opcje: wezwij lekarza na oddzial:\n");
    printf("1 - pediatra\n2 - kardiolog\n3 - neurolog\n4 - okulista\n5 - laryngolog\n6 - chirurg\n\n");

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
            kill(0, SIGINT);
            break;
        }
        else if (wybor >= 1 && wybor <= 6)
        {
            if (pid_lekarze[wybor] > 0)
            {
                kill(pid_lekarze[wybor], SIG_LEKARZ_ODDZIAL);
            }
            
        }
        else
        {
            printf("[main] niepoprawnie uzyty sygnal wezwanie na oddzial\n");
        }
    }  
    
    return 0;
}