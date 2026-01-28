#include "wspolne.h"
#include <sys/wait.h>
#include <pthread.h>

int semid = -1;
int semid_limits = -1;
int shmid = -1;
int msgs_ids[20];
pid_t pid_lekarze[10];


volatile int monitor_running = 1;
pthread_t monitor_tid;
pthread_t monitor2_tid;
    

pid_t pid_dyrektor = -1;

volatile sig_atomic_t sprzatanie_trwa = 0;

const char* nazwy_kolejek[LICZBA_SLIMITS] = {
    "Rejestracja", "POZ", "Kardiolog", "Neurolog", 
    "Laryngolog", "Chirurg", "Okulista", "Pediatra"
};

void przeprowadz_ewakuacje() 
{
    StanSOR *stan = (StanSOR*)shmat(shmid, NULL, 0);
    int cel_ewakuacji = 0;
    if (stan != (void*)-1) {
        cel_ewakuacji = stan->liczba_pacjentow_w_srodku;
    }

    printf("\n=== ROZPOCZYNAM EWAKUACJE SOR (Osob w srodku: %d) ===\n", cel_ewakuacji);

    kill(0, SIG_EWAKUACJA); 

    while(wait(NULL) > 0); 

    if (stan != (void*)-1 && stan) 
    {
        printf("\n=== RAPORT EWAKUACJI ===\n");
        printf("Ewakuowano: %d / %d pacjentow\n", stan->ewakuowani, cel_ewakuacji);
        
        if (stan->ewakuowani == cel_ewakuacji) printf("Status: SUKCES (Wszyscy bezpieczni)\n");
        else printf("Status: UWAGA (Brakuje %d osob - mozliwe ze wlasnie wychodzily)\n", cel_ewakuacji - stan->ewakuowani);

        
        
        shmdt(stan);
    }
}

void* watek_monitor_kolejki(void* arg) 
{
    
    FILE *f = fopen(RAPORT_1, "w");
    if(f) { 
        fprintf(f, "CZAS;KOLEJKA;WOLNE_SEM;ZAJETE_SEM;MSG_COUNT\n"); 
        fclose(f); 
    }

    struct msqid_ds buf;
    while (monitor_running) {
        usleep(1000000); 
        
        if (semid_limits != -1) {
            unsigned short stany[LICZBA_SLIMITS];
            union semun arg;
            arg.array = stany;

            if (semctl(semid_limits, 0, GETALL, arg) != -1) {
                
                time_t now = time(NULL);
                char *t_str = ctime(&now);
                t_str[strlen(t_str)-1] = '\0'; 

                int wolne_sor = semctl(semid, SEM_MIEJSCA_SOR, GETVAL, 0);
                if (wolne_sor != -1)
                {
                    zapisz_raport(RAPORT_1, semid, 
                                  "%s;GLOWNY_HOL_SOR;%d;%d;-\n",
                                  t_str, wolne_sor, MAX_PACJENTOW - wolne_sor);
                }
                

                for (int i = 0; i < LICZBA_SLIMITS; i++) 
                {
                    int zajete_sem = INT_LIMIT_KOLEJEK - stany[i];
                    
                    int faktyczne_msg = -1;
                    if (msgs_ids[i] != -1) {
                        if (msgctl(msgs_ids[i], IPC_STAT, &buf) == 0) {
                            faktyczne_msg = buf.msg_qnum; 
                        }
                    }

                    if (zajete_sem > 0 || faktyczne_msg > 0) 
                    {
                        zapisz_raport(RAPORT_1, semid, 
                                      "%s;%s;%d;%d;%d\n", 
                                      t_str, 
                                      nazwy_kolejek[i], 
                                      stany[i],      // wolne wedlug semafora
                                      zajete_sem,    // zajete wedlug semafora
                                      faktyczne_msg); // faktycznie w kolejce (msqid_ds)
                    }
                }
                zapisz_raport(RAPORT_1, semid, "---\n");
            }
        }
    }
    return NULL;
}

void* watek_monitor_bramki(void* arg)
{
    FILE *f = fopen(RAPORT_2, "w");
    if(f) 
    { 
        fprintf(f, "LOGI AKTYWNOSCI OKIENKA NR 2\n"); 
        fclose(f); 
    }

    StanSOR *stan = (StanSOR*)shmat(shmid, NULL, 0);
    if (stan == (void*)-1) 
    {
        perror("Monitor bramki: blad shmat");
        return NULL;
    }

    int ostatni_stan_bramki = -1; 
    int licznik_cykli = 0; 

    while (monitor_running) 
    {
        usleep(200000); 
        licznik_cykli++;
        
        int obecny_stan = stan->czy_okienko_2_otwarte;
        int dlugosc = stan->dlugosc_kolejki_rejestracji;
        
        if (obecny_stan != ostatni_stan_bramki) 
        {
            zapisz_raport(RAPORT_2, semid, 
                          "[ZMIANA] Kolejka: %d osob | Okienko 2: %s\n", 
                          dlugosc, 
                          obecny_stan ? "OTWARTE" : "ZAMKNIETE");
            ostatni_stan_bramki = obecny_stan;
        }
        else if (dlugosc > (MAX_PACJENTOW / 2) && obecny_stan == 0) 
        {
            zapisz_raport(RAPORT_2, semid, 
                          "[ALARM] Kolejka %d osob, a Okienko 2 wciaz ZAMKNIETE\n", dlugosc);
        }
        else if (dlugosc < (MAX_PACJENTOW / 3) && obecny_stan == 1) 
        {
            zapisz_raport(RAPORT_2, semid, 
                          "[ALARM] Kolejka %d osob, a Okienko 2 wciaz OTWARTE\n", dlugosc);
        }
        
        
        if (licznik_cykli % 5 == 0) 
        {
             zapisz_raport(RAPORT_2, semid, 
                          "[STATUS] Kolejka: %d | Okienko 2: %s\n", 
                          dlugosc, obecny_stan ? "OTWARTE" : "ZAMKNIETE");
        }
    }
    
    shmdt(stan); 
    return NULL;
}

void czyszczenie() 
{     
    if (shmid != -1) {
        StanSOR *stan = (StanSOR*)shmat(shmid, NULL, 0);
        if (stan != (void*)-1) {
            podsumowanie(stan); 
            shmdt(stan);
        }
        shmctl(shmid, IPC_RMID, NULL);
    }

    if (semid != -1) semctl(semid, 0, IPC_RMID);
    if (semid_limits != -1) semctl(semid_limits, 0, IPC_RMID);

    for (int i = 0; i < 20; i++) {
        if (msgs_ids[i] != -1) msgctl(msgs_ids[i], IPC_RMID, NULL);
    }
    
    printf("\n[SYSTEM] Wykonano czyszczenie zasobow IPC.\n");
}

void signal_handler(int sig) 
{
    if (sig == SIGINT) {
        printf("\n[MAIN] PRZERWANIE (CTRL+C). Zamykanie...\n");
        
        if (sprzatanie_trwa) return;
        sprzatanie_trwa = 1;
        monitor_running = 0;
        pthread_join(monitor_tid, NULL);
        pthread_join(monitor2_tid, NULL);

        przeprowadz_ewakuacje();
        
        czyszczenie();
        exit(0);
    }
}

int msg_creat(int index_w_tablicy, int klucz_char) {
    int id = msgget(ftok(FILE_KEY, klucz_char), IPC_CREAT | 0600);
    if (id == -1) perror("Blad msgget");
    msgs_ids[index_w_tablicy] = id;
    return id;
}

pid_t uruchom_proces(const char* prog, const char* process_name, const char* arg1) {
    pid_t pid = fork();
    if (pid == 0) {
        if(arg1) execl(prog, process_name, arg1, NULL);
        else execl(prog, process_name, NULL);
        
        perror("blad execl");
        exit(1);
    }
    return pid;
}

int main(int argc, char *argv[])
{
    signal(SIGINT, signal_handler);
    signal(SIG_EWAKUACJA, SIG_IGN); 

    key_t key_shm = ftok(FILE_KEY, ID_SHM_MEM); 
    key_t key_sem = ftok(FILE_KEY, ID_SEM_SET);
    key_t key_limits = ftok(FILE_KEY, ID_SEM_LIMITS);

    for(int i=0; i<20; i++) msgs_ids[i] = -1;
    msg_creat(0, ID_KOLEJKA_REJESTRACJA);
    msg_creat(1, ID_KOLEJKA_POZ);
    msg_creat(2, ID_KOL_KARDIOLOG);
    msg_creat(3, ID_KOL_NEUROLOG);
    msg_creat(4, ID_KOL_LARYNGOLOG);
    msg_creat(5, ID_KOL_CHIRURG);
    msg_creat(6, ID_KOL_OKULISTA);
    msg_creat(7, ID_KOL_PEDIATRA);

    shmid = shmget(key_shm, sizeof(StanSOR), IPC_CREAT | 0600);
    StanSOR * stan = (StanSOR*)shmat(shmid, NULL, 0);
    memset(stan, 0, sizeof(StanSOR));
    stan->symulacja_trwa = 1;
    for (int i = 1; i <= 6; i++)
    {
        stan->dostepni_specjalisci[i] = 1;
    }
    shmdt(stan);

    semid = semget(key_sem, 5, IPC_CREAT | 0600);
    union semun arg;
    arg.val = 1; semctl(semid, SEM_DOSTEP_PAMIEC, SETVAL, arg);
    arg.val = MAX_PACJENTOW; semctl(semid, SEM_MIEJSCA_SOR, SETVAL, arg);
    arg.val = 1; semctl(semid, SEM_ZAPIS_PLIK, SETVAL, arg);
    arg.val = MAX_PROCESOW; semctl(semid, SEM_GENERATOR, SETVAL, arg);
    arg.val = 0; semctl(semid, SEM_BRAMKA_2, SETVAL, arg);

    semid_limits = semget(key_limits, LICZBA_SLIMITS, IPC_CREAT | 0600);
    unsigned short wartosci[LICZBA_SLIMITS];
    for (int i = 0; i < LICZBA_SLIMITS; i++) wartosci[i] = INT_LIMIT_KOLEJEK;
    arg.array = wartosci;
    semctl(semid_limits, 0, SETALL, arg);

    printf("[MAIN] Start systemu SOR...\n");

    uruchom_proces("./rejestracja", "SOR_Rejestracja", "1");
    uruchom_proces("./rejestracja", "SOR_Rejestracja", "2");
    uruchom_proces("./lekarz", "SOR_POZ", "0"); 



    const char* nazwy_lek[] = {
        "",              // 0 
        "SOR_SPEC_Kardiolog",  // 1 - LEK_KARDIOLOG
        "SOR_SPEC_Neurolog",  // 2 - LEK_NEUROLOG
        "SOR_SPEC_Laryngolog",   // 3 - LEK_LARYNGOLOG
        "SOR_SPEC_Chirurg",   // 4 - LEK_CHIRURG
        "SOR_SPEC_Okulista",   // 5 - LEK_OKULISTA
        "SOR_SPEC_Pediatra"    // 6 - LEK_PEDIATRA
    };



    for(int i=1; i<=6; i++) 
    {
        char buff[5];
        sprintf(buff, "%d", i); 
        pid_lekarze[i] = uruchom_proces("./lekarz", nazwy_lek[i], buff);
    }



    FILE *f = fopen(RAPORT_3, "w");
    if(f) fclose(f); 


    if (argc > 1 && strcmp(argv[1], "auto") == 0) pid_dyrektor = fork();
    
    if (pid_dyrektor == 0) 
    {
       
        StanSOR *stan_child = (StanSOR*)shmat(shmid, NULL, 0);
        
        srand(time(NULL) ^ getpid());
        zapisz_raport(KONSOLA, semid, "[DYREKTOR] Rozpoczynam dyzur.\n");
        
        
        
        while(stan_child->symulacja_trwa) 
        {
            int lek = (rand() % 6) + 1;
            if (stan->dostepni_specjalisci[lek] == 0) continue;

            if (pid_lekarze[lek] > 0) 
            {
                kill(pid_lekarze[lek], SIG_LEKARZ_ODDZIAL);               

            }

            if (!stan_child->symulacja_trwa) break; 
            sleep(rand() % 5 + 2); 
        }
        shmdt(stan_child);
        exit(0);
    }

    

    if (pthread_create(&monitor_tid, NULL, watek_monitor_kolejki, NULL) != 0) 
    {
        perror("Nie udalo sie utworzyc watku monitorujacego");
    }

    if (pthread_create(&monitor2_tid, NULL, watek_monitor_bramki, NULL) != 0) 
    {
        perror("Blad tworzenia watku bramki");
    }

    
    

    pid_t pid_gen = uruchom_proces("./generator", "SOR_Generator", NULL);

    printf("[MAIN] Symulacja 24h w toku...\n");
    
    int status;
    waitpid(pid_gen, &status, 0);

    if (!sprzatanie_trwa) 
    {
        sprzatanie_trwa = 1;

        printf("\n[MAIN] Generator zakonczyl prace. Zamykanie systemu...\n");

        stan = (StanSOR*)shmat(shmid, NULL, 0);
        if (stan != (void*)-1) {
            stan->symulacja_trwa = 0;
            shmdt(stan);
        }

        if (pid_dyrektor > 0) {
            kill(pid_dyrektor, SIGKILL); 
            waitpid(pid_dyrektor, NULL, 0); 
        }

        monitor_running = 0;
        pthread_join(monitor_tid, NULL);
        pthread_join(monitor2_tid, NULL);

        przeprowadz_ewakuacje();

        czyszczenie();

    }
    return 0;
}