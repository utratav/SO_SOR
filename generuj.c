#define _GNU_SOURCE

#include "wspolne.h"
#include <sys/wait.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>

int semid = -1;
volatile int petla = 1;

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
  
void handle_sigint(int sig)
{
    petla = 0;
}


int main(int argc, char* argv[]) 
{
    srand(time(NULL) ^ getpid());

    key_t key_sem = ftok(FILE_KEY, ID_SEM_SET);
    semid = semget(key_sem, 0, 0); // Pobieramy istniejący zestaw
    if (semid == -1) 
    {
        perror("generator: blad semget (uruchom najpierw main!)");
        exit(1);
    }

    
    struct sigaction sa;
    sa.sa_handler = handle_sigchld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP; 
    sigaction(SIGCHLD, &sa, NULL);

    signal(SIGINT, handle_sigint);

    zapisz_raport(FILE_DEST, semid, "[generator] start\n");

    struct sembuf zajmij = {SEM_GENERATOR, -1, 0};

    //int ile = 3;
    while(petla)    
    {
        if (semop(semid, &zajmij, 1) == -1) 
        {
            if (errno == EINTR) continue; 
            if (petla) perror("generator: semop error");
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
            
        }

        //ile--;

        //sleep(1); 
        
    }



    zapisz_raport(FILE_DEST, semid, "[generator] koniec pracy\n");
    return 0;
}
