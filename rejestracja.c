#define _GNU_SOURCE
#include "wspolne.h"

int nr_okienka;
int semid = -1;
volatile sig_atomic_t koniec_pracy = 0;

void handle_sig(int sig) {
    if (sig == SIGINT) koniec_pracy = 1;
}

int main(int argc, char*argv[]) 
{
    struct sigaction sa;
    sa.sa_handler = handle_sig;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    
    if (argc < 2) exit(1);
    nr_okienka = atoi(argv[1]);

    semid = semget(ftok(FILE_KEY, ID_SEM_SET), 0, 0);
    int msgid_we = msgget(ftok(FILE_KEY, ID_KOLEJKA_REJESTRACJA), 0);
    KomunikatPacjenta pacjent;

    while(!koniec_pracy)
    {
        ssize_t status = msgrcv(msgid_we, &pacjent, sizeof(pacjent) - sizeof(long), -2, IPC_NOWAIT);

        if (status == -1) {
            if (errno == ENOMSG || errno == EINTR) {
                usleep(50000);
                continue;
            }
            break;
        }

        pacjent.mtype = pacjent.pacjent_pid; 
        if(msgsnd(msgid_we, &pacjent, sizeof(pacjent) - sizeof(long), 0) != -1)
        {
            if(!koniec_pracy) {
                // Rejestracja obsługuje, ale nie pisze do Raportu 1
                // zapisz_raport(KONSOLA, semid, "[Rejestracja %d] Przyjeto pacjenta %d\n", nr_okienka, pacjent.pacjent_pid);
            }
        }        
    }
    return 0;
}