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
#include <fcntl.h> // Potrzebne do open/O_APPEND

#define FILE_KEY "."

// ID Kolejek
#define ID_KOLEJKA_REJESTRACJA 'R'
#define ID_KOLEJKA_POZ 'P'
#define ID_KOLEJKA_STATYSTYKI 'T'

#define ID_KOL_KARDIOLOG 'K'
#define ID_KOL_NEUROLOG 'N'
#define ID_KOL_LARYNGOLOG 'L'
#define ID_KOL_CHIRURG 'C'
#define ID_KOL_OKULISTA 'O'
#define ID_KOL_PEDIATRA 'D'

#define ID_SHM_MEM 'S'
#define ID_SEM_SET 'M'
#define ID_SEM_LIMITS 'X'

#define PACJENCI_NA_DOBE 50000
#define MAX_PACJENTOW 8000
#define MAX_PROCESOW 10000
#define INT_LIMIT_KOLEJEK 500

// Pliki raportów
#define RAPORT_1 "raport1.txt"
#define RAPORT_2 "raport2.txt"
#define RAPORT_3 "raport3.txt"
#define KONSOLA NULL

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
#define LEK_PEDIATRA 6
#define CZERWONY 1
#define ZOLTY 2
#define ZIELONY 3

#define SIG_EWAKUACJA SIGINT
#define SIG_LEKARZ_ODDZIAL SIGUSR2
#define TYP_VIP 1
#define TYP_ZWYKLY 2

#define STAN_PRZED_SOR 0
#define STAN_W_POCZEKALNI 1
#define STAN_WYCHODZI 2

typedef struct 
{
    int symulacja_trwa;
    int dostepni_specjalisci[7];
    int dlugosc_kolejki_rejestracji;
    int czy_okienko_2_otwarte;
    int ewakuowani_z_poczekalni;
    int ewakuowani_sprzed_sor;
} StanSOR;

typedef struct {
    long mtype;       
    pid_t pacjent_pid; 
    int typ_lekarza;   
    int czy_vip;
    int wiek;
    int kolor;         
    int skierowanie;   
} KomunikatPacjenta;

typedef struct {
    long mtype;
    int czy_vip;
    int kolor;
    int typ_lekarza;
    int skierowanie;
} StatystykaPacjenta;

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

// === NOWA, SZYBKA FUNKCJA LOGOWANIA (BEZ SEMAFORA) ===
static inline void zapisz_raport(const char* filename, int semid, const char* format, ...) {
    (void)semid; // Ignorujemy semid - nie jest juz potrzebny, ale zostawiamy argument dla kompatybilnosci
    
    char bufor[1024]; // Lokalny bufor na sformatowaną linię
    va_list args;

    // 1. Szybkie formatowanie w pamięci RAM
    va_start(args, format);
    int len = vsnprintf(bufor, sizeof(bufor), format, args);
    va_end(args);

    if (len <= 0) return;

    // 2. Atomowy zapis
    if (filename == KONSOLA) {
        // Zapis na standardowe wyjście (deskryptor 1)
        // write jest bezpieczniejszy niż printf przy wielu wątkach
        write(STDOUT_FILENO, bufor, len);
    }
    else {
        // O_APPEND gwarantuje atomowość zapisu na koniec pliku w Linuxie
        // O_CREAT | O_WRONLY | O_APPEND
        // Uprawnienia 0666 (rw-rw-rw-)
        int fd = open(filename, O_WRONLY | O_CREAT | O_APPEND, 0666);
        if (fd != -1) {
            write(fd, bufor, len); // Jeden syscall zamiast fopen/fprintf/fclose
            close(fd);
        }
    }
}

static inline void podsumowanie(StatystykiLokalne *stat, StanSOR *stan)
{
    double p = (double)stat->obs_pacjenci; 
    if (p == 0) p = 1.0; 
    
    int ewak_z_poczekalni = stan->ewakuowani_z_poczekalni;
    int ewak_sprzed_sor = stan->ewakuowani_sprzed_sor;

    char bufor[4096]; // Duży bufor na cały raport końcowy
    int pos = 0;

    // Budujemy cały raport w pamięci, żeby wypisać go "na raz"
    pos += sprintf(bufor + pos, "\n==============================================\n");
    pos += sprintf(bufor + pos, "         RAPORT KONCOWY (PODSUMOWANIE)        \n");
    pos += sprintf(bufor + pos, "==============================================\n");
    pos += sprintf(bufor + pos, "Obsluzeni pacjenci ogolem: %d\n\n", stat->obs_pacjenci);
    pos += sprintf(bufor + pos, "Pacjenci VIP:    %d (oczekiwano ok.: %d)\n", stat->ile_vip, (int)(0.2 * p + 0.5));
    pos += sprintf(bufor + pos, "Pacjenci zwykli: %d (oczekiwano ok.: %d)\n\n", stat->obs_pacjenci - stat->ile_vip, (int)(0.8 * p + 0.5));
    pos += sprintf(bufor + pos, "Pacjenci odeslani do domu przez POZ: %d (oczekiwano ok.: %d)\n", stat->obs_dom_poz, (int)(0.05 * p + 0.5));
    
    pos += sprintf(bufor + pos, "\n--- TRIAZ (KOLORY) ---\n");
    pos += sprintf(bufor + pos, "Czerwony: %d (oczekiwano ok.: %d)\n", stat->obs_kolory[CZERWONY], (int)(0.1 * p + 0.5));
    pos += sprintf(bufor + pos, "Zolty:    %d (oczekiwano ok.: %d)\n", stat->obs_kolory[ZOLTY], (int)(0.35 * p + 0.5));
    pos += sprintf(bufor + pos, "Zielony:  %d (oczekiwano ok.: %d)\n", stat->obs_kolory[ZIELONY], (int)(0.5 * p + 0.5));
    
    pos += sprintf(bufor + pos, "\n--- SPECJALISCI ---\n");
    const char* nazwy_spec[] = {"", "Kardiolog", "Neurolog", "Laryngolog", "Chirurg", "Okulista", "Pediatra"};
    for(int i = 1; i <= 6; i++) pos += sprintf(bufor + pos, "%-12s: %d pacjentow\n", nazwy_spec[i], stat->obs_spec[i]);
    
    pos += sprintf(bufor + pos, "\n--- DECYZJE KONCOWE ---\n");
    pos += sprintf(bufor + pos, "Odeslani do domu:      %d (oczekiwano ok.: %d)\n", stat->decyzja[1], (int)(0.85 * p + 0.5));
    pos += sprintf(bufor + pos, "Skierowani na oddzial: %d (oczekiwano ok.: %d)\n", stat->decyzja[2], (int)(0.145 * p + 0.5));
    pos += sprintf(bufor + pos, "Do innej placowki:     %d (oczekiwano ok.: %d)\n", stat->decyzja[3], (int)(0.005 * p + 0.5));
    
    pos += sprintf(bufor + pos, "\n--- RAPORT EWAKUACJI (TEST SYNCHRONIZACJI) ---\n");
    pos += sprintf(bufor + pos, "Ewakuowani z poczekalni (W_SRODKU): %d\n", ewak_z_poczekalni);
    pos += sprintf(bufor + pos, "Ewakuowani sprzed SOR (W_KOLEJCE):  %d\n", ewak_sprzed_sor);
    pos += sprintf(bufor + pos, "RAZEM (wg StanSOR):                 %d\n", ewak_z_poczekalni + ewak_sprzed_sor);
    pos += sprintf(bufor + pos, "==============================================\n");

    // Jeden atomiczny write na koniec
    write(STDOUT_FILENO, bufor, pos);
}
#endif