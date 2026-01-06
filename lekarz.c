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
    int msgid_spec[2]; //narazie dwoch
    int msgid_spec[1] = msgget(ftok(FILE_KEY, ID_KOLEJKA_KARDIOLOG), 0);
    int msgid_spec[2] = msgget(ftok(FILE_KEY, ID_KOLEJKA_NEUROLOG), 0);  
    int msgid_spec[3] = msgget(ftok(FILE_KEY, ID_KOLEJKA_PEDIATRA), 0);

    if (typ_lekarza == 0)
    {
        praca_poz(msgid_poz, msgid_spec);
    }
    else
    {
        praca_specjalista(msgid_spec[typ_lekarza], msgid_wyn);
    }

    return 0;
}