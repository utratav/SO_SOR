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
int typ_lekarza = 0;    //poz = 0, pozostali specjalisci > 0
volatile int wezwanie_na_oddzial = 0;

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

int int_2_skierowanie(int typ) {
    switch(typ) {
        case 1: return "Pacjent odeslany do domu";
        case 2: return "Pacjent skierowany na oddzial";
        case 3: return "Pacjent skierowany do innej placowki";
       default: return -1;
    }
}

void handle_sig(int sig)
{
    if(sig == SIG_LEKARZ_ODDZIAL)
    {
        wezwanie_na_oddzial = 1;
    }
    else if(sig == SIG_EWAKUACJA)
    {
        zapisz_raport(FILE_DEST, semid, "[lekarz] sygnal ewakuacja\n");
        exit(0);
    }
}


void praca_poz(int msgid_poz, int msgid_wyn)
{
    KomunikatPacjenta pacjent;   

    while(1)
    {
        if(msgrcv(msgid_poz, &pacjent, sizeof(pacjent) - sizeof(long), 0, 0) == -1)
        {
            if (errno != EINTR)
            {
                perror("poz - blad msgrcv");
                continue;
            }
        } 

        char buf[100];
        sprintf(buf, "[POZ] wykonuje podstawowe badania na pacjencie %d, nadaje priorytet\n",
        pacjent.pacjent_pid);
        zapisz_raport(FILE_DEST, semid, buf);


       int r = rand() % 100;
        if (r < 10) 
        {
            pacjent.kolor = CZERWONY;
        }
        else if (r < 45) // 10 + 35 = 45
        {
            pacjent.kolor = ZOLTY;
        }
        else 
        {
            pacjent.kolor = ZIELONY;
        }

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

        int r_dom = rand() % 100;
        if (r_dom < 5)
        {
            pacjent.typ_lekarza = -1;
            pacjent.skierowanie = 1;  
            
            char buf_dom[100];
            sprintf(buf_dom, "[POZ] Pacjent %d zdrowy - odeslany do domu (5%%)\n", pacjent.pacjent_pid);
            zapisz_raport(FILE_DEST, semid, buf_dom);
        }

        pacjent.mtype = pacjent.pacjent_pid;

        if(msgsnd(msgid_wyn, &pacjent, sizeof(pacjent) - sizeof(long), 0) == -1)
        {
            perror("poz - blad wysylania do specjalisty");
        }


    }
}

void praca_specjalista(int typ_lekarza, int msgid_spec, int msgid_wyn)
{
    KomunikatPacjenta pacjent;

    const char* jaki_lekarz = int_to_lekarz(typ_lekarza);

    while(1)
    {
        if(wezwanie_na_oddzial)
        {
            char buf[100];
            sprintf(buf, "[%s] wezwanie na oddzial", jaki_lekarz);
            zapisz_raport(FILE_DEST, semid, buf);
            sleep(5);
            char buff[100];
            sprintf(buff, "[%s] wracam na SOR");
            zapisz_raport(FILE_DEST, semid, buff);
            wezwanie_na_oddzial = 0;
        }

        if(msgrcv(msgid_spec, &pacjent, sizeof(pacjent) - sizeof(long), -3, 0) == -1)
        {
            if (errno != EINTR) continue;
            perror("blad msgrcv");
            break;            
        }

        int r = rand() % 1000;

        int skierowanie = 0;
        if (r < 850) skierowanie = 1; //do domu
        else if (r < 995) skierowanie = 2; //na oddzial
        else skierowanie = 3; //do innej placowki

        char buf[100];
        sprintf(buf, "[%s] %s %s\n", 
            jaki_lekarz, dialog[typ_lekarza], int_2_skierowanie(skierowanie));
        zapisz_raport(FILE_DEST, semid, buf);

        pacjent.skierowanie = skierowanie;

        pacjent.mtype = pacjent.pacjent_pid;

        if (msgsnd(msgid_wyn, &pacjent, sizeof(pacjent) - sizeof(long), 0) == -1)
        {
            perror("specjalista - blad msgsnd");
            
        }
    }
}

const char* dialog[6] = {
    "pobieram probke krwi...",
    "podpinam diody do mozgu...",
    "badam uszy...",
    "badam malego pacjenta",
    "badam oczy...",
    "przeprowadzam operacje"
};
                                                       

int main(int argc, char*argv[])
{
    
    struct sigaction sa;
    sa.sa_handler = handle_sig;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; 
    sigaction(SIG_LEKARZ_ODDZIAL, &sa, NULL);
    signal(SIG_EWAKUACJA, handle_sig);

    

    if (argc < 2)
    {
        fprintf(stderr, "zle uzyles programu. ");
        exit(1);
    }

    typ_lekarza = atoi(argv[1]);

    int msgid_poz = msgget(ftok(FILE_KEY, ID_KOLEJKA_POZ), 0);
    int msgid_wyn = msgget(ftok(FILE_KEY, ID_KOLEJKA_WYNIKI), 0);
    semid = semget(ftok(FILE_KEY, ID_SEM_SET), 0, 0); 


    
    int msgid_spec[10];

    for(int i =1; i <= 6; i++)
    {
        int symbol = int_2_msgid(i);
        msgid_spec[i] = msgget(ftok(FILE_KEY, symbol), 0);
        if (msgid_spec[i] == -1)
        {
            perror("blad msgget");
            exit(1);
        }
    }
   

    if (typ_lekarza == 0)
    {
        praca_poz(msgid_poz, msgid_wyn);
    }
    else
    {
        praca_specjalista(typ_lekarza, msgid_spec[typ_lekarza], msgid_wyn);
    }

    return 0;
}