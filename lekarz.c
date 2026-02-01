#define _GNU_SOURCE

#include "wspolne.h"
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <sys/types.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

int semid = -1;
int shmid = -1;
int typ_lekarza = 0;    
volatile sig_atomic_t wezwanie_na_oddzial = 0;
volatile sig_atomic_t koniec_pracy = 0;

int int_2_msgid(int typ) {
    switch(typ) {
        case LEK_POZ:        return ID_KOLEJKA_POZ;
        case LEK_KARDIOLOG:  return ID_KOL_KARDIOLOG;
        case LEK_NEUROLOG:   return ID_KOL_NEUROLOG;
        case LEK_LARYNGOLOG: return ID_KOL_LARYNGOLOG;
        case LEK_CHIRURG:    return ID_KOL_CHIRURG;
        case LEK_OKULISTA:   return ID_KOL_OKULISTA;
        case LEK_PEDIATRA:   return ID_KOL_PEDIATRA;
        default: return -1;
    }
}

const char* int_to_lekarz(int typ) {
    switch(typ) {
        case LEK_POZ: return "POZ";
        case LEK_KARDIOLOG: return "Kardiolog";
        case LEK_NEUROLOG: return "Neurolog";
        case LEK_LARYNGOLOG: return "Laryngolog";
        case LEK_CHIRURG: return "Chirurg";
        case LEK_OKULISTA: return "Okulista";
        case LEK_PEDIATRA: return "Pediatra";
        default: return "Lekarz";
    }
}

const char* int_2_skierowanie(int typ) {
    switch(typ) {
        case 1: return "odeslany do domu";
        case 2: return "skierowany na oddzial";
        case 3: return "skierowany do innej placowki";
        default: return "blad";
    }
}

const char* dialog[7] = {
    "",                               
    "pobieram probke krwi...",        // 1 - Kardiolog
    "podpinam diody do mozgu...",     // 2 - Neurolog
    "badam uszy...",                  // 3 - Laryngolog
    "przeprowadzam operacje...",      // 4 - Chirurg
    "badam oczy...",                  // 5 - Okulista
    "badam malego pacjenta..."        // 6 - Pediatra
};

void handle_sig(int sig)
{
    if(sig == SIG_LEKARZ_ODDZIAL)
    {
        wezwanie_na_oddzial = 1;
    }
    else if(sig == SIGTERM)
    {
        // SIGTERM = koniec pracy (wysyłany przez main przy ewakuacji/zamknięciu)
        zapisz_raport(KONSOLA, semid, "[%s] Otrzymano sygnal zakonczenia pracy.\n", int_to_lekarz(typ_lekarza));
        koniec_pracy = 1;
    }
}

void praca_poz(int msgid_poz)
{
    KomunikatPacjenta pacjent;   

    while(!koniec_pracy)
    {
        

        if(msgrcv(msgid_poz, &pacjent, sizeof(pacjent) - sizeof(long), 0, IPC_NOWAIT) == -1)
        {
            if (errno == ENOMSG) {
                usleep(100000);
                continue;
            }
            if (errno == EINTR) continue;
            perror("poz - blad msgrcv");
            continue;
        } 

        zapisz_raport(KONSOLA, semid,"[POZ] wykonuje podstawowe badania na pacjencie %d, nadaje priorytet\n", pacjent.pacjent_pid);

        int chory = 1;
        int r = rand() % 100;
        if (r < 10) 
        {
            pacjent.kolor = CZERWONY;
        }
        else if (r < 45)
        {
            pacjent.kolor = ZOLTY;
        }
        else if (r < 95)
        {
            pacjent.kolor = ZIELONY;
        }
        else
        {
            chory = 0;
            pacjent.typ_lekarza = 0;
            pacjent.skierowanie = 1;  
            
            zapisz_raport(KONSOLA, semid, "[POZ] Pacjent %d zdrowy - odeslany do domu\n", pacjent.pacjent_pid);
        }

        if (chory)
        {
            int id_specjalisty;

            if (pacjent.wiek < 18)
            {
                id_specjalisty = LEK_PEDIATRA; 
            }
            else
            {
                id_specjalisty = (rand() % 5) + 1; 
            }
            
            pacjent.typ_lekarza = id_specjalisty;
        }

        pacjent.mtype = pacjent.pacjent_pid;

        zapisz_raport(KONSOLA, semid, "[POZ] Przekazanie Pacjenta %d do %s\n", pacjent.pacjent_pid, int_to_lekarz(pacjent.typ_lekarza));

        if(msgsnd(msgid_poz, &pacjent, sizeof(pacjent) - sizeof(long), 0) == -1)
        {
            if (errno != EINTR)
                perror("poz - blad wysylania do specjalisty");
        }
    }
}

void praca_specjalista(int typ_lekarza, int msgid_spec)
{
    StanSOR *stan = (StanSOR*)shmat(shmid, NULL, 0);

    struct sembuf lock = {SEM_DOSTEP_PAMIEC, -1, SEM_UNDO};
    struct sembuf unlock = {SEM_DOSTEP_PAMIEC, 1, SEM_UNDO};

    KomunikatPacjenta pacjent;
    const char* jaki_lekarz = int_to_lekarz(typ_lekarza);

    while(!koniec_pracy)
    {
        if(wezwanie_na_oddzial)
        {
            semop(semid, &lock, 1);
            stan->dostepni_specjalisci[typ_lekarza] = 0;
            zapisz_raport(RAPORT_3, semid, 
                    "Kardiolog:\t%d\nNeurolog:\t%d\nLaryngolog:\t%d\nChirurg:\t%d\nOkulista:\t%d\nPediatra:\t%d\n\n\n",
                stan->dostepni_specjalisci[1],
                stan->dostepni_specjalisci[2],
                stan->dostepni_specjalisci[3],
                stan->dostepni_specjalisci[4],
                stan->dostepni_specjalisci[5],
                stan->dostepni_specjalisci[6]);
            semop(semid, &unlock, 1);
            zapisz_raport(RAPORT_1, semid, "\n[SIGNAL 2 %s] wezwanie na oddzial\n\n", jaki_lekarz);
            zapisz_raport(KONSOLA, semid, "[SIGNAL 2 %s] wezwanie na oddzial\n", jaki_lekarz);

            sleep(10);

            semop(semid, &lock, 1);
            stan->dostepni_specjalisci[typ_lekarza] = 1;
            zapisz_raport(RAPORT_3, semid, 
                    "Kardiolog:\t%d\nNeurolog:\t%d\nLaryngolog:\t%d\nChirurg:\t%d\nOkulista:\t%d\nPediatra:\t%d\n\n\n",
                stan->dostepni_specjalisci[1],
                stan->dostepni_specjalisci[2],
                stan->dostepni_specjalisci[3],
                stan->dostepni_specjalisci[4],
                stan->dostepni_specjalisci[5],
                stan->dostepni_specjalisci[6]);
            semop(semid, &unlock, 1);
            zapisz_raport(RAPORT_1, semid, "\n[SIGNAL 2 %s] wracam na SOR\n\n", jaki_lekarz);
            zapisz_raport(KONSOLA, semid, "[SIGNAL 2 %s] wracam na SOR\n", jaki_lekarz);
            
            wezwanie_na_oddzial = 0;
        }

        // Użyj IPC_NOWAIT żeby móc sprawdzać koniec_pracy
        if(msgrcv(msgid_spec, &pacjent, sizeof(pacjent) - sizeof(long), -3, IPC_NOWAIT) == -1)
        {
            if (errno == ENOMSG) {
                usleep(100000);
                continue;
            }
            if (errno == EINTR) continue;
            perror("blad msgrcv");
            break;            
        }

        int r = rand() % 1000;

        int skierowanie = 0;
        if (r < 850) skierowanie = 1;
        else if (r < 995) skierowanie = 2;
        else skierowanie = 3;

        zapisz_raport(KONSOLA, semid, "[%s] %s Pacjent %d %s\n", 
            jaki_lekarz, dialog[typ_lekarza], pacjent.pacjent_pid, int_2_skierowanie(skierowanie));

        pacjent.skierowanie = skierowanie;
        pacjent.mtype = pacjent.pacjent_pid;

        if (msgsnd(msgid_spec, &pacjent, sizeof(pacjent) - sizeof(long), 0) == -1)
        {
            if (errno != EINTR)
                perror("specjalista - blad msgsnd");
        }
    }
    
    shmdt(stan);
}

int main(int argc, char*argv[])
{
    // Obsługa sygnałów
    struct sigaction sa;
    sa.sa_handler = handle_sig;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIG_LEKARZ_ODDZIAL, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    
    // Ignoruj SIGINT - lekarze nie reagują bezpośrednio na Ctrl+C
    signal(SIGINT, SIG_IGN);
    
    // SIGTSTP (Ctrl+Z) i SIGCONT - domyślne zachowanie
    signal(SIGTSTP, SIG_DFL);
    signal(SIGCONT, SIG_DFL);

    if (argc < 2)
    {
        fprintf(stderr, "zle uzyles programu. ");
        exit(1);
    }

    typ_lekarza = atoi(argv[1]);

    int msgid_poz = msgget(ftok(FILE_KEY, ID_KOLEJKA_POZ), 0);
    semid = semget(ftok(FILE_KEY, ID_SEM_SET), 0, 0); 
    shmid = shmget(ftok(FILE_KEY, ID_SHM_MEM), 0, 0);

    int msgid_spec[10];
    for(int i = 1; i <= 6; i++)
    {
        int symbol = int_2_msgid(i);
        msgid_spec[i] = msgget(ftok(FILE_KEY, symbol), 0);
        if (msgid_spec[i] == -1)
        {
            perror("blad msgget");
            exit(1);
        }
    }

    srand(time(NULL) ^ getpid());

    if (typ_lekarza == 0)
    {
        praca_poz(msgid_poz);
    }
    else
    {
        praca_specjalista(typ_lekarza, msgid_spec[typ_lekarza]);
    }

    zapisz_raport(KONSOLA, semid, "[%s] Koncze prace.\n", int_to_lekarz(typ_lekarza));
    return 0;
}