
#define _GNU_SOURCE

#include "wspolne.h"

int nr_okienka;
int semid = -1;
volatile sig_atomic_t koniec_pracy = 0;

void handle_sig(int sig)
{
    if (sig == SIGINT) 
    {
        zapisz_raport(KONSOLA, semid, "[Rejestracja] koniec pracy okienka %d", nr_okienka);
        _exit(0);
    }
}

int main(int argc, char*argv[]) 
{
    struct sigaction sa;
    sa.sa_handler = handle_sig;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    
    
    signal(SIGTSTP, SIG_DFL);
    signal(SIGCONT, SIG_DFL);

    if (argc < 2)
    {
        fprintf(stderr, "uzycie: %s <nr_okienka>\n", argv[0]);
        exit(1);
    }

    nr_okienka = atoi(argv[1]);

    semid = semget(ftok(FILE_KEY, ID_SEM_SET), 0, 0);
    int msgid_we = msgget(ftok(FILE_KEY, ID_KOLEJKA_REJESTRACJA), 0);

    if (semid == -1 || msgid_we == -1) {
        perror("rejestracja - Blad polaczenia IPC"); 
        exit(1);
    }

    KomunikatPacjenta pacjent;

    

    while(1)
    {
        
        ssize_t status = msgrcv(msgid_we, &pacjent, sizeof(KomunikatPacjenta) - sizeof(long), -2, IPC_NOWAIT);

        if (status == -1)
        {
            if (errno == ENOMSG) {
                usleep(100000);
                continue;
            }
            if (errno == EINTR) continue;
            perror("rejestracja - blad msgrcv");
            continue;
        }

        

        pacjent.mtype = pacjent.pacjent_pid; 
        if(msgsnd(msgid_we, &pacjent, sizeof(KomunikatPacjenta) - sizeof(long), 0) == -1)
        {
            if (errno != EINTR)
                perror("Rejestracja - blad msgsnd");
        }
        else
        {
            if(koniec_pracy) break;
            zapisz_raport(KONSOLA, semid, "[Rejestracja %d] pacjent %d przekazany do POZ\n",
                nr_okienka, pacjent.pacjent_pid);
        }        
    }
    
    
    return 0;
}
