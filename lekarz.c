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
volatile wezwanie_na_oddzial = 0;

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

void handle_sig(int sig)
{
    if(sig == SIG_LEKARZ_ODDZIAL)
    {
        wezwanie_na_oddzial = 1;
    }
    else if(sig == SIG_EWAKUACJA)
    {
        exit(0);
    }
}


void praca_poz(int msgid_poz, int msgid_wyn, int *msgid_spec)
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

        int r = rand() % 100;
        if (r < 5)
        {
            pacjent.mtype = pacjent.pacjent_pid;
            strcpy(pacjent.opis_objawow, "zdrowy - odeslano do domu po POZ");
            msgsnd(msgid_wyn, &pacjent, sizeof(pacjent) - sizeof(long), 0);
            continue;
        }
        else if (r < 10) pacjent.mtype = CZERWONY;
        else if (r < 35) pacjent.mtype = ZOLTY;
        else pacjent.mtype = ZIELONY;

        int id_specjalisty;

        if (pacjent.wiek < 18)
        {
            id_specjalisty = 1; //zobacz potem
        }
        else
        {
            id_specjalisty = rand() % 6 + 1; //nie moze byc zero 
        }

        pacjent.typ_lekarza = id_specjalisty;


        if(msgsnd(msgid_spec[id_specjalisty], &pacjent, sizeof(pacjent) - sizeof(long), 0) == -1)
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
            printf("lekarz %s wezwany na oddzial...", jaki_lekarz);
            sleep(5);
            printf("lekarz %s wrocil na SOR...", jaki_lekarz);
            wezwanie_na_oddzial = 0;
        }

        if(msgrcv(msgid_spec, &pacjent, sizeof(pacjent) - sizeof(long), -3, 0) == -1)
        {
            if (errno != EINTR) continue;
            perror("blad msgrcv");
            break;            
        }

        printf("%s", dialog[typ_lekarza]);

        pacjent.mtype = pacjent.pacjent_pid;

        if (msgsnd(msgid_wyn, &pacjent, sizeof(pacjent) - sizeof(long), 0) == -1)
        {
            perror("specjalista - blad msgsnd");
            
        }
    }
}

const char* dialog[6] = {
    "kardiolog: pobieram probke krwi...",
    "neurolog: podpinam diody do mozgu...",
    "laryngolog: badam uszy...",
    "pediatra: pediatra cos tam robi",
    "okulista: badam oczy...",
    "chirurg: przeprowadzam operacje"
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
        praca_poz(msgid_poz, msgid_wyn, msgid_spec);
    }
    else
    {
        praca_specjalista(typ_lekarza, msgid_spec[typ_lekarza], msgid_wyn);
    }

    return 0;
}