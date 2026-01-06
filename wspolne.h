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

//parametry dla ftok

#define FILE_KEY "."

#define ID_SHM_MEM 'M'
#define ID_SEM_SET 'S'

#define ID_KOLEJKA_REJESTRACJA 'R'
#define ID_KOLEJKA_POZ 'T'
#define ID_KOLEJKA_KARDIOLOG 'K'  
#define ID_KOLEJKA_NEUROLOG 'N'

#define MAX_PACJENTOW 20 //N
#define LIMIT_KOLEJKI_K 7 //K-prog otwracia drugiej


//priorytety dla mtype

#define TYP_VIP 1
#define TYP_ZWYKLY 2

#define CZERWONY 1
#define ZOLTY 2
#define ZZIELONY 3


#define KARDIOLOG 1
#define NEUROLOG 2
#define OKULISTA 3

#define SIG_LEKARZ_ODDZIAL SIGUSR1
#define SIG_EWAKUACJA SIGUSR2



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

    int obs_czerwoni;
    int obs_zolci;
    int obs_zieloni;
    
} StanSOR;

//indeksy dla sem

#define SEM_DOSTEP_PAMIEC 0 //bin
#define SEM_MIEJSCA_SOR 1 //counter

//semctl

union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

#endif