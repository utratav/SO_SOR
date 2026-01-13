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
#include <time.h>

#define FILE_DEST RAPORT_1

#define RAPORT_1 "raport1.txt"
#define RAPORT_2 "raport2.txt"
#define RAPORT_3 "raport3.txt"
#define RAPORT_4 "raport4.txt"

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
#define LIMIT_KOLEJKI_K (MAX_PACJENTOW / 2) //K-prog otwracia drugiej


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
    int kolor;
    int skierowanie;

    

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

    int decyzja[3];
    
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

static void zapisz_raport(const char* nazwa_pliku, int semid, const char* tresc)
 {
    
    struct sembuf lock;
    lock.sem_flg = 0;
    lock.sem_num = SEM_ZAPIS_PLIK;
    lock.sem_op = -1;

    struct sembuf unlock;
    unlock.sem_flg = 0;
    unlock.sem_num = SEM_ZAPIS_PLIK;
    unlock.sem_op = 1;
    
    if (semid != -1) semop(semid, &lock, 1); 

    FILE *f = fopen(nazwa_pliku, "a"); 
    if (f != NULL) {
        fprintf(f, "%s\n", tresc); 
        fclose(f);
    } else {
        perror("Blad otwarcia pliku raportu");
    }

    if (semid != -1) semop(semid, &unlock, 1); 
}

static void zapisz_czas(const char* nazwa_pliku, int semid) 
{
    
    
    time_t czas_surowy;
    struct tm *czas_info;
    char bufor_czasu[80];

    time(&czas_surowy);
    czas_info = localtime(&czas_surowy);
    // Formatowanie daty: Godzina:Minuta:Sekunda
    strftime(bufor_czasu, 80, "%H:%M:%S", czas_info);

    struct sembuf lock = {SEM_ZAPIS_PLIK, -1, 0};
    struct sembuf unlock = {SEM_ZAPIS_PLIK, 1, 0};
    
    if (semid != -1) semop(semid, &lock, 1);

    FILE *f = fopen(nazwa_pliku, "a");
    if (f != NULL) {
        
        fprintf(f, "-----------------------\n");
        fprintf(f, "[%s] %s\n", bufor_czasu);
        fprintf(f, "-----------------------\n");
        fclose(f);
    }

    if (semid != -1) semop(semid, &unlock, 1);
}

#endif //koniec 