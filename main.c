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
pthread_t raport2_tid;

volatile sig_atomic_t ewakuacja_rozpoczeta = 0;

StatystykiLokalne statystyki;
pthread_mutex_t stat_mutex = PTHREAD_MUTEX_INITIALIZER;

// ... (watek_raport_specjalistow BEZ ZMIAN)
void* watek_raport_specjalistow(void* arg) {
    StanSOR *stan = (StanSOR*)shmat(shmid, NULL, 0);
    if (stan == (void*)-1) return NULL;
    const char* nazwy_spec[] = {"", "Kardiolog", "Neurolog", "Laryngolog", "Chirurg", "Okulista", "Pediatra"};
    while(monitor_running) {
        FILE *f = fopen(RAPORT_2, "w");
        if (f) {
            for(int i=1; i<=6; i++) fprintf(f, "%-12s %d\n", nazwy_spec[i], stan->dostepni_specjalisci[i]);
            fclose(f);
        }
        usleep(200000); 
    }
    shmdt(stan);
    return NULL;
}

// ... (watek_statystyki BEZ ZMIAN)
void* watek_statystyki(void* arg) {
    StatystykaPacjenta msg;
    while (monitor_running) {
        if (msgrcv(msgid_stat, &msg, sizeof(StatystykaPacjenta) - sizeof(long), 0, IPC_NOWAIT) == -1) {
            if (errno == ENOMSG || errno == EINTR) { usleep(50000); continue; }
            break;
        }
        pthread_mutex_lock(&stat_mutex);
        statystyki.obs_pacjenci++;
        if (msg.czy_vip) statystyki.ile_vip++;
        if (msg.kolor > 0 && msg.kolor <= 3) statystyki.obs_kolory[msg.kolor]++;
        if (msg.typ_lekarza == 0) statystyki.obs_dom_poz++;
        else if (msg.typ_lekarza >= 1 && msg.typ_lekarza <= 6) statystyki.obs_spec[msg.typ_lekarza]++;
        if (msg.skierowanie >= 1 && msg.skierowanie <= 3) statystyki.decyzja[msg.skierowanie]++;
        pthread_mutex_unlock(&stat_mutex);
    }
    while (msgrcv(msgid_stat, &msg, sizeof(StatystykaPacjenta) - sizeof(long), 0, IPC_NOWAIT) != -1) {
        pthread_mutex_lock(&stat_mutex);
        statystyki.obs_pacjenci++;
        if (msg.czy_vip) statystyki.ile_vip++;
        if (msg.kolor > 0 && msg.kolor <= 3) statystyki.obs_kolory[msg.kolor]++;
        if (msg.typ_lekarza == 0) statystyki.obs_dom_poz++;
        else if (msg.typ_lekarza >= 1 && msg.typ_lekarza <= 6) statystyki.obs_spec[msg.typ_lekarza]++;
        if (msg.skierowanie >= 1 && msg.skierowanie <= 3) statystyki.decyzja[msg.skierowanie]++;
        pthread_mutex_unlock(&stat_mutex);
    }
    return NULL;
}

// --- WĄTEK BRAMKI (TYLKO WYKONAWCA) ---
void* watek_bramka(void* arg)
{
    StanSOR *stan = (StanSOR*)shmat(shmid, NULL, 0);
    if (stan == (void*)-1) return NULL;
    
    int local_okienko_otwarte = 0;
    
    // Czyścimy raport na start
    FILE *f = fopen(RAPORT_1, "w"); if(f) fclose(f);

    while (monitor_running) {
        usleep(5000); // 50ms (szybka reakcja)

        // Odczyt flagi ustawionej przez pacjenta (ATOMOWY)
        int rozkaz = stan->wymuszenie_otwarcia;
        
        // Synchronizacja stanu dla innych
        if (stan->czy_okienko_2_otwarte != local_okienko_otwarte) {
             stan->czy_okienko_2_otwarte = local_okienko_otwarte;
        }

        if (!local_okienko_otwarte && rozkaz == 1) {
            pid_t pid = fork();
            if (pid == 0) {               
                signal(SIGTERM, SIG_DFL);
                execl("./rejestracja", "SOR_rejestracja", "2", NULL);
                exit(1);
            } else if (pid > 0) {
                pid_rejestracja_2 = pid;
                local_okienko_otwarte = 1;
                stan->czy_okienko_2_otwarte = 1;
                
                // Potwierdzenie akcji w raporcie
                zapisz_raport(RAPORT_1, semid, "[MONITOR] Otwieram bramke nr 2\n");
                zapisz_raport(KONSOLA, semid, "[MAIN] Otwieram bramke nr 2\n");
            }
        } 
        else if (local_okienko_otwarte && rozkaz == 0) {
            if (pid_rejestracja_2 > 0) {
                kill(pid_rejestracja_2, SIGINT);
                waitpid(pid_rejestracja_2, NULL, 0);
                pid_rejestracja_2 = -1;
                local_okienko_otwarte = 0;
                stan->czy_okienko_2_otwarte = 0;
                
                // Potwierdzenie akcji w raporcie
                zapisz_raport(RAPORT_1, semid, "[MONITOR] Zamykam bramke nr 2\n");
                zapisz_raport(KONSOLA, semid, "[MAIN] Zamykam bramke nr 2\n");
            }
        }
    }
    if (local_okienko_otwarte && pid_rejestracja_2 > 0) {
        kill(pid_rejestracja_2, SIGTERM);
        waitpid(pid_rejestracja_2, NULL, 0);
    }
    shmdt(stan);
    return NULL;
}

// ... (reszta pliku main.c: czyszczenie, signal_handler, uruchom_proces, main - BEZ ZMIAN)
// Skopiuj resztę pliku main.c z poprzedniej odpowiedzi
void czyszczenie() {     
    if (shmid != -1) shmctl(shmid, IPC_RMID, NULL);
    if (semid != -1) semctl(semid, 0, IPC_RMID);
    if (semid_limits != -1) semctl(semid_limits, 0, IPC_RMID);
    for (int i = 0; i < 20; i++) if (msgs_ids[i] != -1) msgctl(msgs_ids[i], IPC_RMID, NULL);
    if (msgid_stat != -1) msgctl(msgid_stat, IPC_RMID, NULL);
    printf("\n[SYSTEM] Wykonano czyszczenie zasobow IPC.\n");
}

void signal_handler(int sig) {
    if (sig == SIGINT) ewakuacja_rozpoczeta = 1;
}

int msg_creat(int index, int klucz) {
    int id = msgget(ftok(FILE_KEY, klucz), IPC_CREAT | 0600);
    msgs_ids[index] = id;
    return id;
}

pid_t uruchom_proces(const char* prog, const char* name, const char* arg1) {
    pid_t pid = fork();
    if (pid == 0) {
        signal(SIGINT, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        if(arg1) execl(prog, name, arg1, NULL);
        else execl(prog, name, NULL);
        exit(1);
    }
    return pid;
}

void przeprowadz_ewakuacje() {
    printf("\n=== ROZPOCZYNAM EWAKUACJE SOR ===\n");
    if (pid_rejestracja_1 > 0) kill(pid_rejestracja_1, SIGTERM);
    if (pid_poz > 0) kill(pid_poz, SIGTERM);
    for(int i=1; i<=6; i++) if(pid_lekarze[i] > 0) kill(pid_lekarze[i], SIGTERM);
    usleep(10000); 
    if (pid_gen > 0) {
        kill(pid_gen, SIGINT);
        waitpid(pid_gen, NULL, 0);
    }
    if (pid_dyrektor > 0) kill(pid_dyrektor, SIGKILL);
    while(wait(NULL) > 0);
}

int main(int argc, char *argv[]) {
    memset(&statystyki, 0, sizeof(statystyki));
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    signal(SIGTERM, SIG_IGN);

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
    shmid = shmget(key_shm, sizeof(StanSOR), IPC_CREAT | 0600);
    StanSOR *stan = (StanSOR*)shmat(shmid, NULL, 0);
    memset(stan, 0, sizeof(StanSOR));
    stan->symulacja_trwa = 1;
    for (int i = 1; i <= 6; i++) stan->dostepni_specjalisci[i] = 1;
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

    printf("[MAIN] Start systemu SOR... (Max pacjentow: %d)\n", MAX_PACJENTOW);
    pid_rejestracja_1 = uruchom_proces("./rejestracja", "SOR_rejestracja", "1");
    pid_poz = uruchom_proces("./lekarz", "lekarz", "0"); 
    const char* nazwy_lek[] = {"", "SOR_S_Kardiolog", "SOR_S_Neurolog", "SOR_S_Laryngolog", "SOR_S_Chirurg", "SOR_S_Okulista", "SOR_S_Pediatra"};
    for(int i=1; i<=6; i++) {
        char buff[5]; sprintf(buff, "%d", i); 
        pid_lekarze[i] = uruchom_proces("./lekarz", nazwy_lek[i], buff);
    }
    
    FILE *f = fopen(RAPORT_1, "w"); if(f) fclose(f);
    f = fopen(RAPORT_2, "w"); if(f) fclose(f);
    

    if (argc > 1 && strcmp(argv[1], "auto") == 0) {
        pid_dyrektor = fork();
        if (pid_dyrektor == 0) {
            signal(SIGINT, SIG_IGN);
            signal(SIGTERM, SIG_IGN); 
            StanSOR *stan_child = (StanSOR*)shmat(shmid, NULL, 0);
            srand(time(NULL) ^ getpid());
            while(stan_child->symulacja_trwa) {
                int lek = (rand() % 6) + 1;
                if (stan_child->dostepni_specjalisci[lek]) {
                    if (pid_lekarze[lek] > 0) kill(pid_lekarze[lek], SIG_LEKARZ_ODDZIAL);
                }
                sleep(rand() % 5 + 2);
            }
            exit(0);
        }
    }

    pthread_create(&stat_tid, NULL, watek_statystyki, NULL);
    pthread_create(&bramka_tid, NULL, watek_bramka, NULL);
    pthread_create(&raport2_tid, NULL, watek_raport_specjalistow, NULL); 

    pid_gen = uruchom_proces("./generator", "SOR_generator", NULL);

    while (1) {
        if (ewakuacja_rozpoczeta) {
            monitor_running = 0;
            przeprowadz_ewakuacje();
            break;
        }
        int status;
        if (waitpid(pid_gen, &status, WNOHANG) == pid_gen) break;
        usleep(100000);
    }

    if (!ewakuacja_rozpoczeta) {
        monitor_running = 0;
        printf("\n[MAIN] Koniec symulacji. Zamykanie...\n");
        stan = (StanSOR*)shmat(shmid, NULL, 0);
        if (stan != (void*)-1) { stan->symulacja_trwa = 0; shmdt(stan); }
        if (pid_rejestracja_1 > 0) kill(pid_rejestracja_1, SIGTERM);
        if (pid_poz > 0) kill(pid_poz, SIGTERM);
        for(int i=1; i<=6; i++) if(pid_lekarze[i] > 0) kill(pid_lekarze[i], SIGTERM);
        if (pid_dyrektor > 0) kill(pid_dyrektor, SIGKILL);
        while(wait(NULL) > 0);
    }

    pthread_join(stat_tid, NULL);
    pthread_join(bramka_tid, NULL);
    pthread_join(raport2_tid, NULL);
    
    stan = (StanSOR*)shmat(shmid, NULL, 0);
    if (stan != (void*)-1) {
        podsumowanie(&statystyki, stan);
        shmdt(stan);
    }

    czyszczenie();
    return 0;
}