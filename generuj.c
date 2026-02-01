#define _GNU_SOURCE
#include "wspolne.h"

int semid = -1;
volatile sig_atomic_t ewakuacja = 0;
volatile sig_atomic_t sigchld_received = 0;

void handle_ewakuacja(int sig) { ewakuacja = 1; }
void handle_sigchld(int sig) { sigchld_received = 1; }

void zbierz_zombie() {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        if (semid != -1) {
            struct sembuf unlock = {SEM_GENERATOR, 1, SEM_UNDO};
            semop(semid, &unlock, 1);
        }
    }
    sigchld_received = 0;
}

void procedura_ewakuacji() {
    printf("\n[GENERATOR] Rozpoczynam ewakuacje pacjentow (wysylam SIGINT)...\n");
    
    kill(0, SIGINT); // Wyślij do grupy
    
    int status;
    pid_t pid;
    int suma_exit_code = 0;
    
    while ((pid = waitpid(-1, &status, 0)) > 0) {
        if (WIFEXITED(status)) {
            suma_exit_code += WEXITSTATUS(status);
        }
        if (errno == ECHILD) break;
    }
    
    printf("\n[GENERATOR] Ewakuacja zakonczona.\n");
    printf("[GENERATOR] Suma kodow wyjscia (kontrolna): %d\n", suma_exit_code);
    printf("[GENERATOR] (Ta liczba powinna byc rowna sumie w raporcie koncowym)\n");
}

int main(int argc, char* argv[])
{
    struct sigaction sa;
    sa.sa_handler = handle_sigchld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);

    sa.sa_handler = handle_ewakuacja;
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL); 
    
    semid = semget(ftok(FILE_KEY, ID_SEM_SET), 0, 0);
    if (semid == -1) exit(1);

    srand(time(NULL) ^ getpid());
    zapisz_raport(KONSOLA, semid, "\n[GENERATOR] Start.\n");

    for (int i = 0; i < PACJENCI_NA_DOBE; i++)
    {
        if (ewakuacja) break;
        if (sigchld_received) zbierz_zombie();
        
        struct sembuf zajmij = {SEM_GENERATOR, -1, SEM_UNDO};
        if (semop(semid, &zajmij, 1) == -1) {
            if (errno == EINTR) { if (ewakuacja) break; i--; continue; }
            break;
        }
        
        pid_t pid = fork();
        if (pid == 0) {
            execl("./pacjent", "pacjent", NULL);
            exit(1);
        } else if (pid == -1) {
            struct sembuf oddaj = {SEM_GENERATOR, 1, 0};
            semop(semid, &oddaj, 1);
            usleep(10000);
            i--;
        }
    }

    if (ewakuacja) {
        procedura_ewakuacji();
    } else {
        zapisz_raport(KONSOLA, semid, "[GENERATOR] Koniec generowania. Czekam na dzieci...\n");
        while(wait(NULL) > 0);
    }
    
    return 0;
}