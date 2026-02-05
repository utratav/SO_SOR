#define _GNU_SOURCE
#include "wspolne.h"
#include <sys/msg.h>
#include <stdlib.h> 

int nr_okienka;
int semid = -1;
volatile sig_atomic_t koniec_pracy = 0;

KomunikatPacjenta *bufor_oczekujacych = NULL;
int bufor_licznik = 0;
int bufor_pojemnosc = 0; 

void handle_sig(int sig) {
    if (sig == SIGINT || sig == SIGTERM) koniec_pracy = 1;
}

void pobierz_stan_kolejki(int msqid, struct msqid_ds *buf) {
    if (msgctl(msqid, IPC_STAT, buf) == -1) {
        perror("msgctl error");
        
    }
}

int main(int argc, char*argv[]) 
{
    struct sigaction sa;
    sa.sa_handler = handle_sig;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    signal(SIGTERM, handle_sig);
    
    if (argc < 2) exit(1);
    nr_okienka = atoi(argv[1]);

    semid = semget(ftok(FILE_KEY, ID_SEM_SET), 0, 0);
    int msgid_we = msgget(ftok(FILE_KEY, ID_KOLEJKA_REJESTRACJA), 0);
    
    struct msqid_ds stan_kolejki;
    pobierz_stan_kolejki(msgid_we, &stan_kolejki);

    size_t rozmiar_pojedynczej_wiadomosci = sizeof(KomunikatPacjenta) - sizeof(long);

 
    int max_msg_limit = stan_kolejki.msg_qbytes / rozmiar_pojedynczej_wiadomosci;


    bufor_pojemnosc = max_msg_limit; 
    
   
    bufor_oczekujacych = (KomunikatPacjenta*)malloc(bufor_pojemnosc * sizeof(KomunikatPacjenta));
    if (bufor_oczekujacych == NULL) {
        perror("Błąd malloc");
        exit(1);
    }



    KomunikatPacjenta pacjent;


    while(!koniec_pracy)
    {
        pobierz_stan_kolejki(msgid_we, &stan_kolejki);

        if (bufor_licznik > 0) {
            

            if (stan_kolejki.msg_qnum < max_msg_limit) { 
                
                for (int i = 0; i < bufor_licznik; i++) {
                    if (msgsnd(msgid_we, &bufor_oczekujacych[i], rozmiar_pojedynczej_wiadomosci, IPC_NOWAIT) != -1) {
                        zapisz_raport(KONSOLA, semid, "[ Rejestracja %d ] Wznowiono z bufora: %d\n", nr_okienka, bufor_oczekujacych[i].pacjent_pid);
                        
                        for(int j=i; j<bufor_licznik-1; j++) bufor_oczekujacych[j] = bufor_oczekujacych[j+1];
                        bufor_licznik--;
                        i--; 
                    } else {
                        if (errno == EAGAIN) break; 
                    }
                }
            }
        }

        ssize_t status = msgrcv(msgid_we, &pacjent, rozmiar_pojedynczej_wiadomosci, -2, IPC_NOWAIT);

        if (status == -1) {
            if (errno == ENOMSG || errno == EINTR) {
                usleep(50000);
                continue;
            }
            break;
        }

        pacjent.mtype = pacjent.pacjent_pid; 

        if (kill(pacjent.pacjent_pid, 0) == -1 && errno == ESRCH) {
            zapisz_raport(KONSOLA, semid, "[ Rejestracja %d ] Brak informacji o pacjencie %d (exit), Anuluje wysylanie wiadomosci\n",nr_okienka, pacjent.pacjent_pid);
            continue; 
        }

        if(msgsnd(msgid_we, &pacjent, rozmiar_pojedynczej_wiadomosci, IPC_NOWAIT) != -1)
        {
            if(!koniec_pracy) {
                zapisz_raport(KONSOLA, semid, "[ Rejestracja %d ] Pacjent %d -> POZ\n", nr_okienka, pacjent.pacjent_pid);
            }
        } 
        else if (errno == EAGAIN) 
        {
            if (bufor_licznik < bufor_pojemnosc) {
                bufor_oczekujacych[bufor_licznik++] = pacjent;
                
                pobierz_stan_kolejki(msgid_we, &stan_kolejki);
                zapisz_raport(KONSOLA, semid, "[ Rejestracja %d ] KOLEJKA FULL (%lu/%d msg) Pacjent %d -> BUFOR (%d/%d).\n", 
                             nr_okienka, stan_kolejki.msg_qnum, max_msg_limit, pacjent.pacjent_pid, bufor_licznik, bufor_pojemnosc);
            } else {
                zapisz_raport(KONSOLA, semid, "[CRITICAL] BUFOR PRZEPELNIONY! Pacjent %d porzucony.\n", pacjent.pacjent_pid);
            }
        }       
    }

    if (bufor_oczekujacych) free(bufor_oczekujacych);
    return 0;
}