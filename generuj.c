#include "wspolne.h"




int semid = -1;

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

int main(int argc, char* argv[])
{
    struct sigaction sa;
    sa.sa_handler = handle_sigchld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);

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
        struct sembuf zajmij = {SEM_GENERATOR, -1, SEM_UNDO};
        
        if (semop(semid, &zajmij, 1) == -1)
        {
            if (errno == EINTR) { i--; continue; }
            perror("generator: semop error");
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

    while (wait(NULL) > 0);

    zapisz_raport(KONSOLA, semid, "[GENERATOR] Koniec pracy generatora.\n");
    return 0;
}