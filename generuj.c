#define _GNU_SOURCE
#include "wspolne.h"

int semid = -1;
int shmid = -1;
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

void procedura_ewakuacji(int liczba_wygenerowanych) {
    signal(SIGINT, SIG_IGN); 
    signal(SIGTERM, SIG_IGN); 

    printf("\n[GENERATOR] Robie SNAPSHOT pamieci dzielonej (BLOKADA SWIATA)...\n");

    StanSOR *stan = (StanSOR*)shmat(shmid, NULL, 0);
    if (stan != (void*)-1) {
        struct sembuf lock = {SEM_DOSTEP_PAMIEC, -1, SEM_UNDO};
        struct sembuf unlock = {SEM_DOSTEP_PAMIEC, 1, SEM_UNDO};
        semop(semid, &lock, 1);

        int weszlo = stan->ile_weszlo_ogolem;
        int wyszlo = stan->ile_wyszlo_ogolem;
        
    
        stan->snap_w_srodku = weszlo - wyszlo;
        
    
        stan->snap_przed_sor = liczba_wygenerowanych - weszlo;
        if (stan->snap_przed_sor < 0) stan->snap_przed_sor = 0; // Zabezpieczenie

        printf("[GENERATOR] Snapshot: Wygenerowano=%d, Weszlo=%d, Wyszlo=%d\n", liczba_wygenerowanych, weszlo, wyszlo);
        printf("[GENERATOR] Snapshot: W srodku=%d, Przed SOR=%d\n", stan->snap_w_srodku, stan->snap_przed_sor);

        printf("[GENERATOR] Zabijam pacjentow (SIGTERM)...\n");
        kill(0, SIGTERM);

        semop(semid, &unlock, 1);
        shmdt(stan);
    }

    int suma_exit_code = 0;
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, 0)) > 0) {
        if (WIFEXITED(status)) {
            suma_exit_code += WEXITSTATUS(status);
        }
    }
    
    printf("\n[GENERATOR] Ewakuacja zakonczona.\n");
    printf("[GENERATOR] Suma kodow wyjscia (kontrolna): %d\n", suma_exit_code);
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
    shmid = shmget(ftok(FILE_KEY, ID_SHM_MEM), 0, 0);
    if (semid == -1 || shmid == -1) exit(1);

    srand(time(NULL) ^ getpid());
    zapisz_raport(KONSOLA, semid, "\n[GENERATOR] Start.\n");

    int i = 0;
    for (i = 0; i < PACJENCI_NA_DOBE; i++)
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
        procedura_ewakuacji(i); // Przekazujemy ile faktycznie wygenerowano
    } else {
        zapisz_raport(KONSOLA, semid, "[GENERATOR] Koniec generowania. Czekam na dzieci...\n");
        while(wait(NULL) > 0);
    }
    return 0;
}