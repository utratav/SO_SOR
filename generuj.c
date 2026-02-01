#define _GNU_SOURCE

#include "wspolne.h"

int semid = -1;

volatile sig_atomic_t ewakuacja = 0;
volatile sig_atomic_t sigchld_received = 0;

int ewakuowani_z_poczekalni = 0;
int ewakuowani_sprzed_sor = 0;


void handle_ewakuacja(int sig)
{
    if (sig == SIGINT) 
    {
        ewakuacja = 1;
    }
}

int zbierz_zombie()
{
    int suma = 0;
    int status;
    pid_t pid;
    
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        if (semid != -1)
        {
            struct sembuf unlock = {SEM_GENERATOR, 1, SEM_UNDO};
            semop(semid, &unlock, 1);
        }
        
        if (WIFEXITED(status)) {
            suma += WEXITSTATUS(status);
        }
    }
    sigchld_received = 0;
    return suma;
}

void zabij_dzieci_i_licz_ewakuowanych()
{
    printf("\n[GENERATOR] Rozpoczynam ewakuacje pacjentow...\n");
    
    ewakuowani_sprzed_sor = semctl(semid, SEM_MIEJSCA_SOR, GETNCNT);
    printf("[GENERATOR] Pacjentow czekajacych przed SOR (GETNCNT): %d\n", ewakuowani_sprzed_sor);
    
    kill(0, SIGINT);
    
    printf("[GENERATOR] Wyslano SIGINT do wszystkich potomkow\n");
    
    usleep(200000); 
    
    int suma_miejsc = 0;
    int status;
    pid_t pid;
    
    // Zbieraj przez max 5 sekund
    for (int i = 0; i < 50; i++) {
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
            if (WIFEXITED(status)) {
                suma_miejsc += WEXITSTATUS(status);
            }
        }
        if (pid == -1 && errno == ECHILD) break;
        usleep(100000);
    }
    
    // Ostatnie zbieranie
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        if (WIFEXITED(status)) {
            suma_miejsc += WEXITSTATUS(status);
        }
    }
    
    ewakuowani_z_poczekalni = suma_miejsc;
    printf("[GENERATOR] Ewakuowano (suma miejsc z exit codes): %d\n", suma_miejsc);
}

int main(int argc, char* argv[])
{
    struct sigaction sa_chld;
    sa_chld.sa_handler = handle_sigchld;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa_chld, NULL);

    struct sigaction sa_int;
    sa_int.sa_handler = handle_ewakuacja;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = 0;
    sigaction(SIGINT, &sa_int, NULL);
    
    signal(SIGTSTP, SIG_DFL);
    signal(SIGCONT, SIG_DFL);

    key_t key_sem = ftok(FILE_KEY, ID_SEM_SET);
    
    semid = semget(key_sem, 0, 0);
    if (semid == -1) {
        perror("generator: blad semget");
        exit(1);
    }

    srand(time(NULL) ^ getpid());

    zapisz_raport(KONSOLA, semid, "\n[GENERATOR] Start symulacji. Cel: %d pacjentow (24h).\n", PACJENCI_NA_DOBE);

    for (int i = 0; i < PACJENCI_NA_DOBE && !ewakuacja; i++)
    {
        if (sigchld_received) {
            zbierz_zombie();
        }
        
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
            signal(SIGCHLD, SIG_DFL);
            execl("./pacjent", "pacjent", NULL);
            perror("generator: execl failed");
            exit(1);
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
        
        zabij_dzieci_i_licz_ewakuowanych();
        
        zapisz_raport(KONSOLA, semid, "[GENERATOR] Ewakuacja zakonczona. Koncze prace.\n");
    } else {
        zapisz_raport(KONSOLA, semid, "[GENERATOR] Wszyscy pacjenci wygenerowani. Czekam na wyjscie ostatnich osob...\n");
        
        while (wait(NULL) > 0 || errno == EINTR);
        
        zapisz_raport(KONSOLA, semid, "[GENERATOR] Koniec pracy generatora.\n");
    }
    
    
    if (ewakuacja) {
        printf("[GENERATOR] Statystyki: sprzed_sor=%d, z_poczekalni=%d\n", 
               ewakuowani_sprzed_sor, ewakuowani_z_poczekalni);
    }
    
    return 0;
}
