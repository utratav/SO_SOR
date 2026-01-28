#include "wspolne.h"




int semid = -1;
volatile sig_atomic_t ewakuacja = 0;

void handle_sigchld(int sig)
{
    int pam_errno = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0)
    {
        if (semid != -1)
        {
            struct sembuf unlock;
            unlock.sem_flg = SEM_UNDO;
            unlock.sem_num = SEM_GENERATOR;
            unlock.sem_op  = 1;
            semop(semid, &unlock, 1);
        }
    }
    errno = pam_errno;
}

void handle_ewakuacja(int sig)
{
    ewakuacja = 1;
}

int main(int argc, char* argv[])
{
    struct sigaction sa;
    sa.sa_handler = handle_sigchld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);

    struct sigaction sa_ewak;
    sa_ewak.sa_handler = handle_ewakuacja;
    sigemptyset(&sa_ewak.sa_mask);
    sa_ewak.sa_flags = 0; 
    sigaction(SIG_EWAKUACJA, &sa_ewak, NULL);

    key_t key_sem = ftok(FILE_KEY, ID_SEM_SET);
    semid = semget(key_sem, 0, 0);
    if (semid == -1) {
        perror("generator: blad semget");
        exit(1);
    }

    srand(time(NULL) ^ getpid());

    zapisz_raport(KONSOLA, semid, "\n[GENERATOR] Start symulacji. Cel: %d pacjentow (24h).\n", PACJENCI_NA_DOBE);

    for (int i = 0; i < PACJENCI_NA_DOBE; i++)
    {
        if (ewakuacja) break;

        struct sembuf zajmij = {SEM_GENERATOR, -1, SEM_UNDO};
        
        if (semop(semid, &zajmij, 1) == -1)
        {
            if (errno == EINTR) { 
                if (ewakuacja) break; // Jeśli przerwał nas sygnał ewakuacji -> koniec
                i--; continue; 
                perror("generator: semop error");
                break;
            }
        }

        if (ewakuacja) 
        {
            struct sembuf oddaj = {SEM_GENERATOR, 1, 0};
            semop(semid, &oddaj, 1);
            break;
        }

        pid_t pid = fork();
        if (pid == 0)
        {
            execl("./pacjent", "pacjent", NULL);
            perror("generator: execl failed");
            exit(1);
        }
        else if (pid == -1)
        {
            perror("generator: fork failed");
            struct sembuf oddaj = {SEM_GENERATOR, 1, 0};
            semop(semid, &oddaj, 1);
            usleep(10000); // Backoff
            i--;
        }
    }

    zapisz_raport(KONSOLA, semid, "[GENERATOR] Wszyscy pacjenci wygenerowani. Czekam na wyjscie ostatnich osob...\n");

    pid_t wpid;
    while (1) {
        wpid = wait(NULL);
        if (wpid == -1) {
            if (errno == EINTR) continue; 
            if (errno == ECHILD) break;   
            break; 
        }
    }

    zapisz_raport(KONSOLA, semid, "[GENERATOR] Koniec pracy generatora.\n");
    return 0;
}



//pkill -SIGUSR2 -f SOR_SPEC