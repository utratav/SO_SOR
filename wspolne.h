#ifndef WSPOLNE_H
#define WSPOLNE_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <stdarg.h> 
#include <pthread.h>
#include <sys/wait.h>

#define FILE_KEY "."
#define ID_KOLEJKA_REJESTRACJA 'R'
#define ID_KOLEJKA_POZ 'P'
#define ID_KOLEJKA_STATYSTYKI 'T'  // NOWA kolejka pacjent->main

#define ID_KOL_KARDIOLOG 'K'
#define ID_KOL_NEUROLOG 'N'
#define ID_KOL_LARYNGOLOG 'L'
#define ID_KOL_CHIRURG 'C'
#define ID_KOL_OKULISTA 'O'
#define ID_KOL_PEDIATRA 'D'

#define ID_SHM_MEM 'S'
#define ID_SEM_SET 'M'
#define ID_SEM_LIMITS 'X'

#define PACJENCI_NA_DOBE 5000
#define MAX_PACJENTOW 800
#define MAX_PROCESOW 1000
#define INT_LIMIT_KOLEJEK 500

#define RAPORT_1 "raport1.txt"
#define RAPORT_2 "raport2.txt"
#define RAPORT_3 "raport3.txt"
#define KONSOLA NULL

// Semafory - uproszczone (bez SEM_BRAMKA_2)
#define SEM_DOSTEP_PAMIEC 0
#define SEM_MIEJSCA_SOR 1
#define SEM_ZAPIS_PLIK 2   
#define SEM_GENERATOR 3
#define LICZBA_SEMAFOROW 4

#define SLIMIT_REJESTRACJA 0
#define SLIMIT_POZ         1
#define SLIMIT_KARDIOLOG   2
#define SLIMIT_NEUROLOG    3
#define SLIMIT_LARYNGOLOG  4
#define SLIMIT_CHIRURG     5
#define SLIMIT_OKULISTA    6
#define SLIMIT_PEDIATRA    7
#define LICZBA_SLIMITS     8

#define LEK_POZ 0
#define LEK_KARDIOLOG 1
#define LEK_NEUROLOG 2
#define LEK_LARYNGOLOG 3
#define LEK_CHIRURG 4
#define LEK_OKULISTA 5
#define LEK_PEDIATRA 6

#define CZERWONY 1
#define ZOLTY 2
#define ZIELONY 3

// Sygnały
#define SIG_EWAKUACJA SIGINT
#define SIG_LEKARZ_ODDZIAL SIGUSR2

#define TYP_VIP 1
#define TYP_ZWYKLY 2

// Stany pacjenta dla ewakuacji
#define STAN_PRZED_SOR 0
#define STAN_W_POCZEKALNI 1
#define STAN_WYCHODZI 2

// Struktura StanSOR - do synchronizacji i licznika kolejki rejestracji
typedef struct 
{
    int symulacja_trwa;
    int dostepni_specjalisci[7];
    int dlugosc_kolejki_rejestracji;  // do dynamicznego otwierania okienka 2
    int czy_okienko_2_otwarte;
    int stan_przed_sor;
    int stan_poczekalnia;
    
} StanSOR;

// Struktura do komunikacji pacjent<->lekarz
typedef struct {
    long mtype;       
    pid_t pacjent_pid; 
    int typ_lekarza;   
    int czy_vip;
    int wiek;
    int kolor;         
    int skierowanie;   
} KomunikatPacjenta;

// NOWA struktura - statystyki wysyłane przez pacjenta do main
typedef struct {
    long mtype;        // zawsze 1
    int czy_vip;
    int kolor;         // CZERWONY/ZOLTY/ZIELONY lub 0 jeśli zdrowy
    int typ_lekarza;   // 0 = odesłany z POZ, 1-6 = specjalista
    int skierowanie;   // 1=dom, 2=oddział, 3=inna placówka

} StatystykaPacjenta;

// Lokalna struktura statystyk w main (nie pamięć dzielona)
typedef struct 
{
    int obs_pacjenci;
    int ile_vip;
    int obs_spec[7];
    int obs_kolory[4];
    int obs_dom_poz;
    int decyzja[4];

} StatystykiLokalne;

union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

static inline void zapisz_raport(const char* filename, int semid, const char* format, ...) {
    va_list args;

    if (filename == KONSOLA) {
        struct sembuf lock = {SEM_ZAPIS_PLIK, -1, SEM_UNDO};
        struct sembuf unlock = {SEM_ZAPIS_PLIK, 1, SEM_UNDO};

        while (semop(semid, &lock, 1) == -1) {
            if (errno != EINTR) return;
        }

        va_start(args, format);
        vprintf(format, args); 
        va_end(args);
        
        fflush(stdout); 

        while (semop(semid, &unlock, 1) == -1) {
            if (errno != EINTR) break;
        }
    }
    else {
        FILE *f = fopen(filename, "a");
        if (f) {
            va_start(args, format);
            vfprintf(f, format, args); 
            va_end(args);
            fclose(f);
        }
    }
}

static inline void podsumowanie(StatystykiLokalne *stat, int ewak_z_poczekalni, int ewak_sprzed_sor)
{
    double p = (double)stat->obs_pacjenci; 
    if (p == 0) p = 1.0; 

    printf("\n==============================================\n");
    printf("         RAPORT KONCOWY (PODSUMOWANIE)        \n");
    printf("==============================================\n");

    printf("Obsluzeni pacjenci ogolem: %d\n\n", stat->obs_pacjenci);

    printf("Pacjenci VIP:    %d (oczekiwano ok.: %d)\n", 
           stat->ile_vip, (int)(0.2 * p + 0.5));
           
    printf("Pacjenci zwykli: %d (oczekiwano ok.: %d)\n\n", 
           stat->obs_pacjenci - stat->ile_vip, (int)(0.8 * p + 0.5));

    printf("Pacjenci odeslani do domu przez POZ: %d (oczekiwano ok.: %d)\n",
             stat->obs_dom_poz, (int)(0.05 * p + 0.5));

    printf("\n--- TRIAZ (KOLORY) ---\n");
    printf("Czerwony: %d (oczekiwano ok.: %d)\n", 
           stat->obs_kolory[CZERWONY], (int)(0.1 * p + 0.5));
    printf("Zolty:    %d (oczekiwano ok.: %d)\n", 
           stat->obs_kolory[ZOLTY], (int)(0.35 * p + 0.5));
    printf("Zielony:  %d (oczekiwano ok.: %d)\n", 
           stat->obs_kolory[ZIELONY], (int)(0.5 * p + 0.5));

    printf("\n--- SPECJALISCI ---\n");
    const char* nazwy_spec[] = {"", "Kardiolog", "Neurolog", "Laryngolog", "Chirurg", "Okulista", "Pediatra"};
    for(int i = 1; i <= 6; i++) {
        printf("%-12s: %d pacjentow\n", nazwy_spec[i], stat->obs_spec[i]);
    }

    printf("\n--- DECYZJE KONCOWE ---\n");
    printf("Odeslani do domu:      %d (oczekiwano ok.: %d)\n", 
           stat->decyzja[1], (int)(0.85 * p + 0.5));
    printf("Skierowani na oddzial: %d (oczekiwano ok.: %d)\n", 
           stat->decyzja[2], (int)(0.145 * p + 0.5));
    printf("Do innej placowki:     %d (oczekiwano ok.: %d)\n", 
           stat->decyzja[3], (int)(0.005 * p + 0.5));

    printf("\n--- RAPORT EWAKUACJI ---\n");
    printf("Ewakuowani z poczekalni:   %d\n", ewak_z_poczekalni);
    printf("Ewakuowani sprzed SOR:     %d\n", ewak_sprzed_sor);
    printf("Razem ewakuowani:          %d\n", ewak_z_poczekalni + ewak_sprzed_sor);

    printf("==============================================\n");
}

#endif
