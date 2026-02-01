#define _GNU_SOURCE

#include "wspolne.h"

int semid = -1;
int shmid = -1;
StanSOR *stan = NULL;

volatile sig_atomic_t ewakuacja = 0;
volatile sig_atomic_t generowanie_aktywne = 1;

// Statystyki ewakuacji
int ewakuowani_suma_miejsc = 0;
pthread_mutex_t zombie_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_t zombie_tid;

// Lista dzieci do ubicia
#define MAX_DZIECI 10000
pid_t lista_dzieci[MAX_DZIECI];
int liczba_dzieci = 0;
pthread_mutex_t dzieci_mutex = PTHREAD_MUTEX_INITIALIZER;

void handle_ewakuacja(int sig)
{
    if (sig == SIGINT) 
    {
        ewakuacja = 1;
        generowanie_aktywne = 0;
    }
}

void dodaj_dziecko(pid_t pid)
{
    pthread_mutex_lock(&dzieci_mutex);
    if (liczba_dzieci < MAX_DZIECI)
    {
        lista_dzieci[liczba_dzieci++] = pid;
    }
    pthread_mutex_unlock(&dzieci_mutex);
}

void usun_dziecko(pid_t pid)
{
    pthread_mutex_lock(&dzieci_mutex);
    for (int i = 0; i < liczba_dzieci; i++)
    {
        if (lista_dzieci[i] == pid)
        {
            lista_dzieci[i] = lista_dzieci[liczba_dzieci - 1];
            liczba_dzieci--;
            break;
        }
    }
    pthread_mutex_unlock(&dzieci_mutex);
}

void ubij_wszystkie_dzieci()
{
    pthread_mutex_lock(&dzieci_mutex);
    for (int i = 0; i < liczba_dzieci; i++)
    {
        if (lista_dzieci[i] > 0)
        {
            kill(lista_dzieci[i], SIGINT);
        }
    }
    pthread_mutex_unlock(&dzieci_mutex);
}

// Wątek zbierający zombie
void* watek_zbieraj_zombie(void* arg)
{
    int status;
    pid_t pid;
    
    while (1)
    {
        // Sprawdź czy są jeszcze aktywne dzieci
        int val = semctl(semid, SEM_GENERATOR, GETVAL);
        if (val == MAX_PROCESOW && !generowanie_aktywne)
        {
            break;
        }
        
        pid = waitpid(-1, &status, WNOHANG);
        
        if (pid > 0) 
        {
            usun_dziecko(pid);
            
            struct sembuf unlock = {SEM_GENERATOR, 1, SEM_UNDO};
            semop(semid, &unlock, 1);
            
            if (WIFEXITED(status)) 
            {
                pthread_mutex_lock(&zombie_mutex);
                ewakuowani_suma_miejsc += WEXITSTATUS(status);
                pthread_mutex_unlock(&zombie_mutex);
            }
        }
        else
        {
            usleep(10000);
        }
    }
    
    return NULL;
}

int main(int argc, char* argv[])
{
    struct sigaction sa_int;
    sa_int.sa_handler = handle_ewakuacja;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = 0;
    sigaction(SIGINT, &sa_int, NULL);
    
    signal(SIGCHLD, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    signal(SIGCONT, SIG_DFL);

    key_t key_sem = ftok(FILE_KEY, ID_SEM_SET);
    key_t key_shm = ftok(FILE_KEY, ID_SHM_MEM);
    
    semid = semget(key_sem, 0, 0);
    if (semid == -1) {
        perror("generator: blad semget");
        exit(1);
    }
    
    shmid = shmget(key_shm, 0, 0);
    if (shmid != -1) {
        stan = (StanSOR*)shmat(shmid, NULL, 0);
        if (stan == (void*)-1) stan = NULL;
    }

    srand(time(NULL) ^ getpid());

    if (pthread_create(&zombie_tid, NULL, watek_zbieraj_zombie, NULL) != 0)
    {
        perror("generator: blad pthread_create");
        exit(1);
    }

    zapisz_raport(KONSOLA, semid, "\n[GENERATOR] Start symulacji. Cel: %d pacjentow (24h).\n", PACJENCI_NA_DOBE);

    for (int i = 0; i < PACJENCI_NA_DOBE && !ewakuacja; i++)
    {
        struct sembuf zajmij = {SEM_GENERATOR, -1, SEM_UNDO};
        
        if (semop(semid, &zajmij, 1) == -1)
        {
            if (errno == EINTR) {
                if (ewakuacja) {
                    zapisz_raport(KONSOLA, semid, "[GENERATOR] Przerwano przez sygnal ewakuacji\n");
                    break;
                }
                i--; 
                continue;
            }
            perror("generator: semop error");
            break;
        }
        
        if (ewakuacja) {
            struct sembuf oddaj = {SEM_GENERATOR, 1, 0};
            semop(semid, &oddaj, 1);
            break;
        }

        pid_t pid = fork();
        if (pid == 0)
        {
            execl("./pacjent", "pacjent", NULL);
            perror("generator: execl failed");
            _exit(1);
        }
        else if (pid > 0)
        {
            dodaj_dziecko(pid);
        }
        else if (pid == -1)
        {
            perror("generator: fork failed");
            struct sembuf oddaj = {SEM_GENERATOR, 1, 0};
            semop(semid, &oddaj, 1);
            usleep(10000);
            i--;
        }
    }

    generowanie_aktywne = 0;
    
    if (ewakuacja) 
    {
        zapisz_raport(KONSOLA, semid, "[GENERATOR] EWAKUACJA! Przestaje generowac pacjentow.\n");
        
        // Odczytaj statystyki
        if (stan != NULL)
        {
            struct sembuf lock = {SEM_DOSTEP_PAMIEC, -1, SEM_UNDO};
            struct sembuf unlock = {SEM_DOSTEP_PAMIEC, 1, SEM_UNDO};
            semop(semid, &lock, 1);
            printf("[GENERATOR] Pacjentow przed SOR: %d\n", stan->stan_przed_sor);
            printf("[GENERATOR] Pacjentow w poczekalni: %d\n", stan->stan_poczekalnia);
            semop(semid, &unlock, 1);
        }
        
        // AKTYWNIE UBIJ WSZYSTKIE DZIECI
        printf("[GENERATOR] Wysylam SIGINT do wszystkich dzieci...\n");
        ubij_wszystkie_dzieci();
    } 
    else 
    {
        zapisz_raport(KONSOLA, semid, "[GENERATOR] Wszyscy pacjenci wygenerowani. Czekam na zakonczenie obslugi...\n");
    }
    
    pthread_join(zombie_tid, NULL);
    
    if (ewakuacja)
    {
        printf("[GENERATOR] Suma miejsc z exit codes: %d\n", ewakuowani_suma_miejsc);
    }
    
    zapisz_raport(KONSOLA, semid, "[GENERATOR] Koniec pracy generatora.\n");
    
    if (stan != NULL) shmdt(stan);
    
    return 0;
}