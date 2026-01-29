
#define _DEFAULT_SOURCE
#include "wspolne.h"

int semid = -1;
int shmid = -1;

/* Globalna flaga ewakuacji */
volatile sig_atomic_t ewakuacja_trwa = 0;

/* Tablica PIDów dzieci */
#define MAX_DZIECI 60000
pid_t pidy_dzieci[MAX_DZIECI];
int liczba_dzieci = 0;

/* Handler dla SIGCHLD - NIE zwalniamy semafora tutaj przy ewakuacji,
 * robimy to w głównej pętli wait() żeby móc zebrać statusy */
void handle_sigchld(int sig)
{
    int pam_errno = errno;
    int status;
    
    /* Podczas normalnej pracy (nie ewakuacji) obsługujemy SIGCHLD normalnie */
    if (!ewakuacja_trwa) {
        while (waitpid(-1, &status, WNOHANG) > 0) {
            if (semid != -1) {
                struct sembuf unlock = {SEM_GENERATOR, 1, SEM_UNDO};
                semop(semid, &unlock, 1);
            }
        }
    }
    /* Podczas ewakuacji nie robimy nic - wait() w main zbierze wszystko */
    
    errno = pam_errno;
}

/* Handler dla SIG_EWAKUACJA */
void handle_ewakuacja(int sig)
{
    if (sig == SIG_EWAKUACJA) {
        ewakuacja_trwa = 1;
    }
}

int main(int argc, char* argv[])
{
    /* KRYTYCZNE: Ignorujemy SIGINT */
    signal(SIGINT, SIG_IGN);
    
    /* Handler dla SIGCHLD */
    struct sigaction sa_chld;
    sa_chld.sa_handler = handle_sigchld;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa_chld, NULL);
    
    /* Handler dla SIG_EWAKUACJA */
    struct sigaction sa_ewak;
    sa_ewak.sa_handler = handle_ewakuacja;
    sigemptyset(&sa_ewak.sa_mask);
    sa_ewak.sa_flags = 0;
    sigaction(SIG_EWAKUACJA, &sa_ewak, NULL);

    key_t key_sem = ftok(FILE_KEY, ID_SEM_SET);
    key_t key_shm = ftok(FILE_KEY, ID_SHM_MEM);
    
    semid = semget(key_sem, 0, 0);
    shmid = shmget(key_shm, 0, 0);
    
    if (semid == -1) {
        perror("generator: blad semget");
        exit(1);
    }
    if (shmid == -1) {
        perror("generator: blad shmget");
        exit(1);
    }

    srand(time(NULL) ^ getpid());

    zapisz_raport(KONSOLA, semid, "\n[GENERATOR] Start symulacji. Cel: %d pacjentow (24h).\n", PACJENCI_NA_DOBE);

    int wygenerowani = 0;
    
    for (int i = 0; i < PACJENCI_NA_DOBE && !ewakuacja_trwa; i++) {
        struct sembuf zajmij = {SEM_GENERATOR, -1, SEM_UNDO};
        
        while (semop(semid, &zajmij, 1) == -1) {
            if (errno == EINTR) {
                if (ewakuacja_trwa) {
                    goto koniec_generowania;
                }
                continue;
            }
            perror("generator: semop error");
            goto koniec_generowania;
        }

        if (ewakuacja_trwa) {
            /* Oddajemy semafor bo nie będziemy forkować */
            struct sembuf oddaj = {SEM_GENERATOR, 1, 0};
            semop(semid, &oddaj, 1);
            break;
        }

        pid_t pid = fork();
        if (pid == 0) {
            execl("./pacjent", "pacjent", NULL);
            perror("generator: execl failed");
            exit(1);
        } else if (pid == -1) {
            perror("generator: fork failed");
            struct sembuf oddaj = {SEM_GENERATOR, 1, 0};
            semop(semid, &oddaj, 1);
            usleep(10000);
            i--;
        } else {
            /* Zapisujemy PID dziecka */
            if (liczba_dzieci < MAX_DZIECI) {
                pidy_dzieci[liczba_dzieci++] = pid;
            }
            wygenerowani++;
        }
    }

koniec_generowania:

    if (ewakuacja_trwa) {
        zapisz_raport(KONSOLA, semid, "[GENERATOR] Otrzymano sygnal ewakuacji. Wysylam do %d dzieci...\n", liczba_dzieci);
        
        /* Wysyłamy sygnał ewakuacji do WSZYSTKICH naszych dzieci */
        for (int i = 0; i < liczba_dzieci; i++) {
            if (pidy_dzieci[i] > 0) {
                kill(pidy_dzieci[i], SIG_EWAKUACJA);
            }
        }
    } else {
        zapisz_raport(KONSOLA, semid, "[GENERATOR] Wszyscy pacjenci wygenerowani. Czekam na wyjscie ostatnich osob...\n");
    }

    /* Zbieramy statusy wyjścia WSZYSTKICH dzieci i sumujemy wagi ewakuowanych */
    int suma_wag_ewakuowanych = 0;
    int liczba_ewakuowanych = 0;
    int status;
    pid_t child_pid;
    
    while ((child_pid = wait(&status)) > 0) {
        if (WIFEXITED(status)) {
            int kod_wyjscia = WEXITSTATUS(status);
            /* Kod wyjścia > 0 oznacza ewakuowanego pacjenta z wagą = kodem 
             * (1 = sam pacjent, 2 = pacjent + opiekun) */
            if (kod_wyjscia > 0) {
                suma_wag_ewakuowanych += kod_wyjscia;
                liczba_ewakuowanych++;
            }
        } else if (WIFSIGNALED(status)) {
            /* Dziecko zabite sygnałem - też liczymy jako ewakuowane (1 miejsce) */
            suma_wag_ewakuowanych += 1;
            liczba_ewakuowanych++;
        }
    }
    
    if (ewakuacja_trwa) {
        zapisz_raport(KONSOLA, semid, "[GENERATOR] Zebrano %d ewakuowanych procesow (waga: %d miejsc)\n", 
                      liczba_ewakuowanych, suma_wag_ewakuowanych);
    }

    /* Aktualizacja pamięci dzielonej - TYLKO generator to robi po ewakuacji */
    if (ewakuacja_trwa) {
        StanSOR *stan = (StanSOR*)shmat(shmid, NULL, 0);
        if (stan != (void*)-1) {
            struct sembuf lock = {SEM_DOSTEP_PAMIEC, -1, SEM_UNDO};
            struct sembuf unlock = {SEM_DOSTEP_PAMIEC, 1, SEM_UNDO};
            
            semop(semid, &lock, 1);
            
            stan->ewakuowani += suma_wag_ewakuowanych;
            stan->liczba_pacjentow_w_srodku -= suma_wag_ewakuowanych;
            if (stan->liczba_pacjentow_w_srodku < 0) {
                stan->liczba_pacjentow_w_srodku = 0;
            }
            
            semop(semid, &unlock, 1);
            
            zapisz_raport(KONSOLA, semid, "[GENERATOR] Zaktualizowano stan: ewakuowano %d miejsc\n", suma_wag_ewakuowanych);
            
            shmdt(stan);
        }
    }

    zapisz_raport(KONSOLA, semid, "[GENERATOR] Koniec pracy generatora.\n");
    return 0;
}