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

#define MAX_PROCESOW 500

#define MAX_PACJENTOW  400//N
#define LIMIT_KOLEJKI_K (MAX_PACJENTOW / 2) //K-prog otwracia drugiej
#define ZAMKNIECIE_KOLEJKI (MAX_PACJENTOW / 3)  

//parametry dla ftok

#define FILE_KEY "."

#define ID_SHM_MEM 'M'
#define ID_SEM_SET 'S'
#define ID_SEM_LIMITS 'L'

#define ID_KOLEJKA_REJESTRACJA 'R' 
#define ID_KOLEJKA_POZ 'T' 

#define ID_KOL_KARDIOLOG   '1'
#define ID_KOL_NEUROLOG    '2'
#define ID_KOL_LARYNGOLOG  '3'
#define ID_KOL_CHIRURG     '4'
#define ID_KOL_OKULISTA    '5'
#define ID_KOL_PEDIATRA    '6'

#define SLIMIT_REJESTRACJA 0
#define SLIMIT_POZ 1
#define SLIMIT_KARDIOLOG 2
#define SLIMIT_NEUROLOG 3
#define SLIMIT_LARYNGOLOG 4
#define SLIMIT_CHIRURG 5
#define SLIMIT_OKULISTA 6
#define SLIMIT_PEDIATRA 7

#define LICZBA_SLIMITS 8
#define INT_LIMIT_KOLEJEK 150




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
#define SEM_GENERATOR 3 //counter



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

    int ile_vip;
    int obs_pacjenci;
    int obs_spec[7]; 
    int obs_kolory[4];
    int obs_dom_poz;
    int sig_wezwano;

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
    lock.sem_flg = SEM_UNDO;
    lock.sem_num = SEM_ZAPIS_PLIK;
    lock.sem_op = -1;

    struct sembuf unlock;
    unlock.sem_flg = SEM_UNDO;
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

static void podsumowanie(StanSOR *stan)
{
    double p = (double)stan->obs_pacjenci;
    if (p == 0) p = 1.0;

    printf("\n----------------------------------------------\n");
    printf("\tpodsumowanie symulacji\n\n");

    printf("obsluzeni pacjenci: %d\n\n", stan->obs_pacjenci);

    printf("liczba pacjentow VIP: %d (docelowo: %d)\n", stan->ile_vip,(int)(0.2 * p + 0.5));
    printf("liczba pacjentow zwyklych: %d (docelowo: %d)\n", stan->obs_pacjenci - stan->ile_vip,(int)(0.8 * p + 0.5));


    printf("pacjenci odeslani do domu bezposrednio po POZ: %d (docelowo: %d)\n",
         stan->obs_dom_poz,(int)(0.05 * p + 0.5));

    printf("\n\tnadane priorytety\n");
    printf("czerwony: %d razy (docelowo: %d)\n", stan->obs_kolory[CZERWONY],(int)(0.1 * p + 0.5));
    printf("zolty: %d razy (docelowo: %d)\n", stan->obs_kolory[ZOLTY], (int)(0.35 * p + 0.5));
    printf("zielony: %d razy (docelowo: %d)\n", stan->obs_kolory[ZIELONY], (int)(0.5 * p + 0.5));

    printf("\n\tobsluzeni specjalisci\n");
    const char* nazwy_spec[] = {"", "Kardiolog", "Neurolog", "Laryngolog", "Chirurg", "Okulista", "Pediatra"};
    for(int i = 1; i <= 6; i++) {
        printf("%s: %d pacjentow\n", nazwy_spec[i], stan->obs_spec[i]);
    }

    printf("\n\tskierowanie dalej\n");
    printf("odeslani do domu: %d (docelowo: %d)\n", stan->decyzja[1], (int)(0.85 * p + 0.5));
    printf("skierowani na oddzial: %d (docelowo: %d)\n", stan->decyzja[2], (int)(0.145 * p + 0.5));
    printf("do innej placowki: %d (docelowo: %d)\n", stan->decyzja[3], (int)(0.005 * p + 0.5));

    printf("----------------------------------------------\n");


}

#endif //koniec 