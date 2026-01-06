#include "wspolne.h"
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <time.h>
#include <errno.h>

int typ_lekarza = 0;    //poz = 0, pozostali specjalisci > 0

void handle_sig(int sig)
{
    if(sig == SIG_LEKARZ_ODDZIAL)
    {
        //DO IMPLEMENTACJI  
    }
}

void praca_poz(int msgid_poz, int msgid_spec, int ilosc_spec)
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
        if (r < 10) pacjent.mtype = CZERWONY;
        else if (r < 45) pacjent.mtype = ZOLTY;
        else pacjent.mtype = ZIELONY;

        int id_specjalisty;

        if (pacjent.wiek < 18)
        {
            id_specjalisty = 3; //zobacz potem
        }
        else
        {
            id_specjalisty = rand() % ilosc_spec + 1; //nie moze byc zero 
        }

        pacjent.typ_lekarza = id_specjalisty;


        if(msgsnd(msgid_spec, &pacjent, sizeof(pacjent) - sizeof(long), 0) == -1)
        {
            perror("poz - blad wysylania do specjalisty");
        }


    }
}

void praca_specjalista(int msgid_spec, int msgid_wyn)
{
    KomunikatPacjenta pacjent;

    while(1)
    {
        if(msgrcv(msgid_spec, &pacjent, sizeof(pacjent) - sizeof(long), -3, 0) == -1)
        {
            if (errno != EINTR)
            {
                perror("specjalista - blad msgrcv");
                continue;
            }
        }

        pacjent.mtype = pacjent.pacjent_pid;

        if (msgsnd(msgid_wyn, &pacjent, sizeof(pacjent) - sizeof(pacjent), 0) == -1)
        {
            perror("specjalista - blad msgsnd");
            
        }
    }
}

int main(int argc, char*argv[])
{
    signal(SIGTERM, handle_sig);
    signal(SIG_LEKARZ_ODDZIAL, handle_sig);

    if (argc < 2)
    {
        fprintf(stderr, "zle uzyles programu. ");
        exit(1);
    }

    typ_lekarza = atoi(argv[1]);

    int msgid_poz = msgget(ftok(FILE_KEY, ID_KOLEJKA_POZ), 0);
    int msgid_wyn = msgget(ftok(FILE_KEY, ID_KOLEJKA_WYNIKI), 0);
    int msgid_spec[3]; //narazie dwoch
    int msgid_spec[1] = msgget(ftok(FILE_KEY, ID_KOLEJKA_KARDIOLOG), 0);
    int msgid_spec[2] = msgget(ftok(FILE_KEY, ID_KOLEJKA_NEUROLOG), 0);  
    int msgid_spec[3] = msgget(ftok(FILE_KEY, ID_KOLEJKA_PEDIATRA), 0);

    int ilosc_spec = sizeof(msgid_spec) / sizeof(msgid_spec[1]);

    if (typ_lekarza == 0)
    {
        praca_poz(msgid_poz, msgid_spec, ilosc_spec);
    }
    else
    {
        praca_specjalista(msgid_spec[typ_lekarza], msgid_wyn);
    }

    return 0;
}