#define _GNU_SOURCE

#include "wspolne.h"
#include <sys/wait.h>
#include <pthread.h>

int semid = -1;
int semid_limits = -1;
int shmid = -1;
int msgid_stat = -1;
int msgs_ids[20];

pid_t pid_lekarze[10];
pid_t pid_rejestracja_1 = -1;
pid_t pid_rejestracja_2 = -1; 
pid_t pid_poz = -1;
pid_t pid_gen = -1;
pid_t pid_dyrektor = -1;

volatile int monitor_running = 1;
pthread_t stat_tid;      
pthread_t bramka_tid;   

volatile sig_atomic_t sprzatanie_trwa = 0;
volatile sig_atomic_t ewakuacja_rozpoczeta = 0;

StatystykiLokalne statystyki;
pthread_mutex_t stat_mutex = PTHREAD_MUTEX_INITIALIZER;

int ewak_z_poczekalni = 0;
int ewak_sprzed_sor = 0;

void* watek_statystyki(void* arg)
{
    StatystykaPacjenta msg;
    
    while (monitor_running) {
        if (msgrcv(msgid_stat, &msg, sizeof(StatystykaPacjenta) - sizeof(long), 0, IPC_NOWAIT) == -1) {
            if (errno == ENOMSG || errno == EINTR) {
                usleep(50000); 
                continue;
            }
            break;
        }
        
        pthread_mutex_lock(&stat_mutex);
        statystyki.obs_pacjenci++;
        if (msg.czy_vip) statystyki.ile_vip++;
        if (msg.kolor > 0 && msg.kolor <= 3) statystyki.obs_kolory[msg.kolor]++;
        if (msg.typ_lekarza == 0) {
            statystyki.obs_dom_poz++;
        } else if (msg.typ_lekarza >= 1 && msg.typ_lekarza <= 6) {
            statystyki.obs_spec[msg.typ_lekarza]++;
        }
        if (msg.skierowanie >= 1 && msg.skierowanie <= 3) {
            statystyki.decyzja[msg.skierowanie]++;
        }
        pthread_mutex_unlock(&stat_mutex);
    }
    
    // Opróżnij kolejkę na koniec
    while (msgrcv(msgid_stat, &msg, sizeof(StatystykaPacjenta) - sizeof(long), 0, IPC_NOWAIT) != -1) {
        pthread_mutex_lock(&stat_mutex);
        statystyki.obs_pacjenci++;
        if (msg.czy_vip) statystyki.ile_vip++;
        if (msg.kolor > 0 && msg.kolor <= 3) statystyki.obs_kolory[msg.kolor]++;
        if (msg.typ_lekarza == 0) {
            statystyki.obs_dom_poz++;
        } else if (msg.typ_lekarza >= 1 && msg.typ_lekarza <= 6) {
            statystyki.obs_spec[msg.typ_lekarza]++;
        }
        if (msg.skierowanie >= 1 && msg.skierowanie <= 3) {
            statystyki.decyzja[msg.skierowanie]++;
        }
        pthread_mutex_unlock(&stat_mutex);
    }
    
    return NULL;
}

void* watek_bramka(void* arg)
{
    StanSOR *stan = (StanSOR*)shmat(shmid, NULL, 0);
    if (stan == (void*)-1) {
        perror("watek_bramka: blad shmat");
        return NULL;
    }
    
    int local_okienko_otwarte = 0;
    
    while (monitor_running) 
    {
        usleep(200000);
        
        int shm_okienko_otwarte = stan->czy_okienko_2_otwarte;
        
        if (!local_okienko_otwarte && shm_okienko_otwarte) 
        {
            pid_t pid = fork();
            if (pid == 0) 
            {               
                execl("./rejestracja", "rejestracja", "2", NULL);
                perror("execl rejestracja 2");
                _exit(1);
            } 
            else if (pid > 0) 
            {
                pid_rejestracja_2 = pid;
                local_okienko_otwarte = 1;
                zapisz_raport(KONSOLA, semid, "[MAIN] Otwieram okienko 2 (kolejka: %d)\n", stan->dlugosc_kolejki_rejestracji);
            }
        }
        else if (local_okienko_otwarte && !shm_okienko_otwarte) 
        {
            if (pid_rejestracja_2 > 0) 
            {
                kill(pid_rejestracja_2, SIGTERM);
                waitpid(pid_rejestracja_2, NULL, 0);
                pid_rejestracja_2 = -1;
                local_okienko_otwarte = 0;
                zapisz_raport(KONSOLA, semid, "[MAIN] Zamykam okienko 2 (kolejka: %d)\n", stan->dlugosc_kolejki_rejestracji);
            }
        }
    }
    
    if (local_okienko_otwarte && pid_rejestracja_2 > 0) 
    {
        kill(pid_rejestracja_2, SIGINT);
        waitpid(pid_rejestracja_2, NULL, 0);
        pid_rejestracja_2 = -1;
    }
    
    shmdt(stan);
    return NULL;
}

void czyszczenie() 
{     
    if (shmid != -1) shmctl(shmid, IPC_RMID, NULL);
    if (semid != -1) semctl(semid, 0, IPC_RMID);
    if (semid_limits != -1) semctl(semid_limits, 0, IPC_RMID);

    for (int i = 0; i < 20; i++) {
        if (msgs_ids[i] != -1) msgctl(msgs_ids[i], IPC_RMID, NULL);
    }
    if (msgid_stat != -1) msgctl(msgid_stat, IPC_RMID, NULL);
    
    printf("\n[SYSTEM] Wykonano czyszczenie zasobow IPC.\n");
}

void signal_handler(int sig) 
{
    if (sig == SIGINT) 
    {
        if (sprzatanie_trwa) return;
        sprzatanie_trwa = 1;
        ewakuacja_rozpoczeta = 1;
        
        printf("\n[MAIN] EWAKUACJA (CTRL+C). Rozpoczynam procedure...\n");
        
        monitor_running = 0;
        
        // Wyślij SIGINT do CAŁEJ grupy procesów (wszystkie dzieci, wnuki, itd.)
        kill(0, SIGINT);
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
        signal(SIGTERM, SIG_DFL);
        
        if(arg1) execl(prog, process_name, arg1, NULL);
        else execl(prog, process_name, NULL);
        
        perror("blad execl");
        _exit(1);
    }
    return pid;
}

int main(int argc, char *argv[])
{
    memset(&statystyki, 0, sizeof(statystyki));
    
    struct sigaction sa_int;
    sa_int.sa_handler = signal_handler;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = 0;
    sigaction(SIGINT, &sa_int, NULL);
    
    signal(SIGTSTP, SIG_DFL);
    signal(SIGCONT, SIG_DFL);

    key_t key_shm = ftok(FILE_KEY, ID_SHM_MEM); 
    key_t key_sem = ftok(FILE_KEY, ID_SEM_SET);
    key_t key_limits = ftok(FILE_KEY, ID_SEM_LIMITS);
    key_t key_stat = ftok(FILE_KEY, ID_KOLEJKA_STATYSTYKI);

    for(int i=0; i<20; i++) msgs_ids[i] = -1;
    for(int i=0; i<10; i++) pid_lekarze[i] = -1;
    
    msg_creat(0, ID_KOLEJKA_REJESTRACJA);
    msg_creat(1, ID_KOLEJKA_POZ);
    msg_creat(2, ID_KOL_KARDIOLOG);
    msg_creat(3, ID_KOL_NEUROLOG);
    msg_creat(4, ID_KOL_LARYNGOLOG);
    msg_creat(5, ID_KOL_CHIRURG);
    msg_creat(6, ID_KOL_OKULISTA);
    msg_creat(7, ID_KOL_PEDIATRA);
    
    msgid_stat = msgget(key_stat, IPC_CREAT | 0600);
    if (msgid_stat == -1) perror("Blad msgget stat");

    shmid = shmget(key_shm, sizeof(StanSOR), IPC_CREAT | 0600);
    StanSOR *stan = (StanSOR*)shmat(shmid, NULL, 0);
    memset(stan, 0, sizeof(StanSOR));
    stan->symulacja_trwa = 1;
    for (int i = 1; i <= 6; i++) {
        stan->dostepni_specjalisci[i] = 1;
    }
    shmdt(stan);

    semid = semget(key_sem, LICZBA_SEMAFOROW, IPC_CREAT | 0600);
    union semun arg;
    arg.val = 1; semctl(semid, SEM_DOSTEP_PAMIEC, SETVAL, arg);
    arg.val = MAX_PACJENTOW; semctl(semid, SEM_MIEJSCA_SOR, SETVAL, arg);
    arg.val = 1; semctl(semid, SEM_ZAPIS_PLIK, SETVAL, arg);
    arg.val = MAX_PROCESOW; semctl(semid, SEM_GENERATOR, SETVAL, arg);

    semid_limits = semget(key_limits, LICZBA_SLIMITS, IPC_CREAT | 0600);
    unsigned short wartosci[LICZBA_SLIMITS];
    for (int i = 0; i < LICZBA_SLIMITS; i++) wartosci[i] = INT_LIMIT_KOLEJEK;
    arg.array = wartosci;
    semctl(semid_limits, 0, SETALL, arg);

    printf("[MAIN] Start systemu SOR...\n");
    printf("[MAIN] Nacisnij Ctrl+C aby rozpoczac ewakuacje.\n\n");

    pid_rejestracja_1 = uruchom_proces("./rejestracja", "SOR_rejestracja", "1");
    pid_poz = uruchom_proces("./lekarz", "SOR_POZ", "0"); 

    const char* nazwy_lek[] = {"", "SOR_Kardiolog", "SOR_Neurolog", "SOR_Laryngolog", "SOR_Chirurg", "SOR_Okulista", "SOR_Pediatra"};
    for(int i=1; i<=6; i++) {
        char buff[5];
        sprintf(buff, "%d", i); 
        pid_lekarze[i] = uruchom_proces("./lekarz", nazwy_lek[i], buff);
    }

    FILE *f = fopen(RAPORT_2, "w");
    if(f) { fprintf(f, "LOGI BRAMKI OKIENKA 2\n"); fclose(f); }
    f = fopen(RAPORT_3, "w");
    if(f) fclose(f);

    if (argc > 1 && strcmp(argv[1], "auto") == 0) {
        pid_dyrektor = fork();
        if (pid_dyrektor == 0) {
            signal(SIGINT, SIG_IGN);
            StanSOR *stan_child = (StanSOR*)shmat(shmid, NULL, 0);
            srand(time(NULL) ^ getpid());
            zapisz_raport(KONSOLA, semid, "[DYREKTOR] Rozpoczynam dyzur.\n");
            
            while(stan_child->symulacja_trwa) {
                int lek = (rand() % 6) + 1;
                if (stan_child->dostepni_specjalisci[lek] == 0) continue;
                if (pid_lekarze[lek] > 0) {
                    kill(pid_lekarze[lek], SIG_LEKARZ_ODDZIAL);
                }
                sleep(rand() % 5 + 2);
            }
            shmdt(stan_child);
            _exit(0);
        }
    }

    if (pthread_create(&stat_tid, NULL, watek_statystyki, NULL) != 0) {
        perror("Blad tworzenia watku statystyk");
    }
    if (pthread_create(&bramka_tid, NULL, watek_bramka, NULL) != 0) {
        perror("Blad tworzenia watku bramki");
    }

    pid_gen = uruchom_proces("./generator", "SOR_generator", NULL);

    printf("[MAIN] Symulacja 24h w toku...\n");
    
    // Główna pętla
    while (1) 
    {
        pid_t finished = waitpid(pid_gen, NULL, WNOHANG);
        
        if (finished == pid_gen) {
            pid_gen = -1;
            break;
        }
        
        if (ewakuacja_rozpoczeta) {
            // Czekaj na generator
            printf("[MAIN] Czekam na generator...\n");
            waitpid(pid_gen, NULL, 0);
            pid_gen = -1;
            break;
        }
        
        usleep(100000);
    }

    // Zatrzymaj wątki
    monitor_running = 0;
    pthread_join(stat_tid, NULL);
    pthread_join(bramka_tid, NULL);

    printf("[MAIN] Generator zakonczyl prace.\n");

    // Wyślij SIGINT do pozostałych procesów (lekarze, rejestracja)
    if (pid_poz > 0) kill(pid_poz, SIGINT);
    for (int i = 1; i <= 6; i++) {
        if (pid_lekarze[i] > 0) kill(pid_lekarze[i], SIGINT);
    }
    if (pid_rejestracja_1 > 0) kill(pid_rejestracja_1, SIGINT);
    if (pid_rejestracja_2 > 0) kill(pid_rejestracja_2, SIGINT);
    if (pid_dyrektor > 0) kill(pid_dyrektor, SIGKILL);

    // Zbierz wszystkie procesy
    printf("[MAIN] Czekam na zakonczenie wszystkich procesow...\n");
    while (wait(NULL) > 0 || errno == EINTR);
    printf("[MAIN] Wszystkie procesy zakonczone.\n");

    // Odczytaj statystyki ewakuacji
    if (ewakuacja_rozpoczeta)
    {
        stan = (StanSOR*)shmat(shmid, NULL, 0);
        if (stan != (void*)-1) {
            ewak_sprzed_sor = stan->stan_przed_sor;
            ewak_z_poczekalni = stan->stan_poczekalnia;
            shmdt(stan);
        }
    }
    else
    {
        stan = (StanSOR*)shmat(shmid, NULL, 0);
        if (stan != (void*)-1) {
            stan->symulacja_trwa = 0;
            shmdt(stan);
        }
    }

    pthread_mutex_lock(&stat_mutex);
    podsumowanie(&statystyki, ewak_z_poczekalni, ewak_sprzed_sor);
    pthread_mutex_unlock(&stat_mutex);

    czyszczenie();

    return 0;
}