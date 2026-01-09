#ifndef WSPOLNE_H
#define WSPOLNE_H

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>


#define FILE_DEST "raport.txt"
//parametry dla ftok

#define FILE_KEY "."

#define ID_SHM_MEM 'M'
#define ID_SEM_SET 'S'

#define ID_KOLEJKA_REJESTRACJA 'R' //pacjent->rejestracja
#define ID_KOLEJKA_POZ 'T'  //rejestracja ->poz
#define ID_KOLEJKA_WYNIKI 'W'   //jeden ze specjalistow -> pacjent

#define ID_KOL_KARDIOLOG   '1'
#define ID_KOL_NEUROLOG    '2'
#define ID_KOL_LARYNGOLOG  '3'
#define ID_KOL_CHIRURG     '4'
#define ID_KOL_OKULISTA    '5'
#define ID_KOL_PEDIATRA    '6'

#define MAX_PACJENTOW 20 //N
#define LIMIT_KOLEJKI_K MAX_PACJENTOW / 2//K-prog otwracia drugiej


//priorytety dla mtype

#define TYP_VIP 1
#define TYP_ZWYKLY 2

#define CZERWONY 1
#define ZOLTY 2
#define ZIELONY 3

#define SIG_LEKARZ_ODDZIAL SIGUSR1
#define SIG_EWAKUACJA SIGUSR2

//indeksy dla sem
#define SEM_DOSTEP_PAMIEC 0 //bin
#define SEM_MIEJSCA_SOR 1 //counter
#define SEM_ZAPIS_PLIK 2 //bin



//struct dla komunikatow

typedef struct {
    long mtype; //mtype bedzie zmieniac na poszczegolnych etapach cyklu, najpierw vip,zwykly, potem kolor

    pid_t pacjent_pid;
    int typ_lekarza;
    int czy_vip;
    int wiek;
    char opis_objawow[50];

    

} KomunikatPacjenta;

//shm

typedef struct {
    int liczba_pacjentow_w_srodku;
    int dlugosc_kolejki_rejestracji;
    int czy_okienko_2_otwarte;

    int obs_pacjenci;
    int obs_spec[7];
    int obs_czerwoni;
    int obs_zolci;
    int obs_zieloni;
    int obs_dom_poz;

    int ods_dom;
    int ods_oddzial;
    int ods_inna
    
} StanSOR;


//semctl

union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};


#define LEK_POZ        0
#define LEK_KARDIOLOG  1
#define LEK_NEUROLOG   2
#define LEK_LARYNGOLOG 3
#define LEK_CHIRURG    4
#define LEK_OKULISTA   5
#define LEK_PEDIATRA   6

#endif //koniec