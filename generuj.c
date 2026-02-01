#define _GNU_SOURCE

#include "wspolne.h"

int semid = -1;
int shmid = -1;
StanSOR *stan = NULL;

// Flaga ewakuacji - ustawiana TYLKO przez SIGINT
volatile sig_atomic_t ewakuacja = 0;

// Lista PIDów dzieci do zabicia przy ewakuacji
#define MAX_DZIECI 10000
pid_t dzieci[MAX_DZIECI];
int liczba_dzieci = 0;
pthread_mutex_t dzieci_mutex = PTHREAD_MUTEX_INITIALIZER;

void handle_sigchld(int sig)
{
    int pam_errno = errno;
    pid_t pid;
    while ((pid = waitpid(-1, NULL, WNOHANG)) > 0)
    {
        if (semid != -1)
        {
            struct sembuf unlock;
            unlock.sem_flg = SEM_UNDO;
            unlock.sem_num = SEM_GENERATOR;
            unlock.sem_op  = 1;
            semop(semid, &unlock, 1);
        }
        
        // Usuń PID z listy dzieci
        pthread_mutex_lock(&dzieci_mutex);
        for (int i = 0; i < liczba_dzieci; i++) {
            if (dzieci[i] == pid) {
                dzieci[i] = dzieci[liczba_dzieci - 1];
                liczba_dzieci--;
                break;
            }
        }
        pthread_mutex_unlock(&dzieci_mutex);
    }
    errno = pam_errno;
}

void handle_ewakuacja(int sig)
{
    // TYLKO SIGINT = ewakuacja
    if (sig == SIGINT) {
        ewakuacja = 1;
    }
}

void zabij_wszystkie_dzieci()
{
    printf("\n[GENERATOR] Rozpoczynam ubijanie procesow pacjentow...\n");
    
    pthread_mutex_lock(&dzieci_mutex);
    int do_zabicia = liczba_dzieci;
    
    // Wyślij SIGINT do wszystkich dzieci (sygnał ewakuacji)
    for (int i = 0; i < liczba_dzieci; i++) {
        if (dzieci[i] > 0) {
            kill(dzieci[i], SIGINT);
        }
    }
    pthread_mutex_unlock(&dzieci_mutex);
    
    printf("[GENERATOR] Wyslano SIGINT do %d pacjentow\n", do_zabicia);
    
    // Czekaj na zakończenie wszystkich dzieci (zbieranie zombie)
    int zebrane = 0;
    pid_t pid;
    int timeout = 0;
    
    while (liczba_dzieci > 0 && timeout < 100) {
        pid = waitpid(-1, NULL, WNOHANG);
        if (pid > 0) {
            zebrane++;
            timeout = 0; // Reset timeout
        } else if (pid == 0) {
            usleep(50000); // 50ms
            timeout++;
        } else {
            if (errno == ECHILD) break;
            if (errno == EINTR) continue;
            break;
        }
    }
    
    // Ostateczne czekanie
    while ((pid = waitpid(-1, NULL, WNOHANG)) > 0) {
        zebrane++;
    }
    
    printf("[GENERATOR] Zebrano %d procesow zombie\n", zebrane);
}

int main(int argc, char* argv[])
{
    // Obsługa SIGCHLD - zbieranie zombie
    struct sigaction sa_chld;
    sa_chld.sa_handler = handle_sigchld;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa_chld, NULL);

    // Obsługa SIGINT (Ctrl+C) - ewakuacja
    struct sigaction sa_int;
    sa_int.sa_handler = handle_ewakuacja;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = 0; // Bez SA_RESTART - chcemy przerwać semop
    sigaction(SIGINT, &sa_int, NULL);
    
    // SIGTSTP (Ctrl+Z) - domyślne zachowanie (zatrzymanie)
    // NIE przechwytujemy - pozwalamy systemowi zatrzymać proces
    signal(SIGTSTP, SIG_DFL);
    
    // SIGCONT - wznowienie po zatrzymaniu
    // Domyślne zachowanie - proces kontynuuje
    signal(SIGCONT, SIG_DFL);

    key_t key_sem = ftok(FILE_KEY, ID_SEM_SET);
    key_t key_shm = ftok(FILE_KEY, ID_SHM_MEM);
    
    semid = semget(key_sem, 0, 0);
    if (semid == -1) {
        perror("generator: blad semget");
        exit(1);
    }
    
    shmid = shmget(key_shm, 0, 0);
    if (shmid == -1) {
        perror("generator: blad shmget");
        exit(1);
    }
    
    stan = (StanSOR*)shmat(shmid, NULL, 0);
    if (stan == (void*)-1) {
        perror("generator: blad shmat");
        exit(1);
    }

    srand(time(NULL) ^ getpid());

    zapisz_raport(KONSOLA, semid, "\n[GENERATOR] Start symulacji. Cel: %d pacjentow (24h).\n", PACJENCI_NA_DOBE);

    for (int i = 0; i < PACJENCI_NA_DOBE && !ewakuacja; i++)
    {
        struct sembuf zajmij = {SEM_GENERATOR, -1, SEM_UNDO};
        
        if (semop(semid, &zajmij, 1) == -1)
        {
            if (errno == EINTR) {
                // Przerwane przez sygnał - sprawdź czy ewakuacja
                if (ewakuacja) {
                    zapisz_raport(KONSOLA, semid, "[GENERATOR] Przerwano przez sygnal ewakuacji\n");
                    break;
                }
                // Jeśli to było SIGTSTP/SIGCONT - kontynuuj normalnie
                i--; 
                continue;
            }
            perror("generator: semop error");
            break;
        }
        
        // Sprawdź ewakuację po semop
        if (ewakuacja) {
            struct sembuf oddaj = {SEM_GENERATOR, 1, 0};
            semop(semid, &oddaj, 1);
            break;
        }

        pid_t pid = fork();
        if (pid == 0)
        {
            // Dziecko
            execl("./pacjent", "pacjent", NULL);
            perror("generator: execl failed");
            exit(1);
        }
        else if (pid > 0)
        {
            // Rodzic - dodaj PID do listy
            pthread_mutex_lock(&dzieci_mutex);
            if (liczba_dzieci < MAX_DZIECI) {
                dzieci[liczba_dzieci++] = pid;
            }
            pthread_mutex_unlock(&dzieci_mutex);
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

    if (ewakuacja) {
        zapisz_raport(KONSOLA, semid, "[GENERATOR] EWAKUACJA! Przestaje generowac pacjentow.\n");
        
        zabij_wszystkie_dzieci();
        
        zapisz_raport(KONSOLA, semid, "[GENERATOR] Ewakuacja zakonczona. Koncze prace.\n");
    } else {
        zapisz_raport(KONSOLA, semid, "[GENERATOR] Wszyscy pacjenci wygenerowani. Czekam na wyjscie ostatnich osob...\n");
        
        while (wait(NULL) > 0);
        
        zapisz_raport(KONSOLA, semid, "[GENERATOR] Koniec pracy generatora.\n");
    }
    
    shmdt(stan);
    return 0;
}