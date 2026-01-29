#define _DEFAULT_SOURCE

#include "wspolne.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <pthread.h>

int semid = -1;
int semid_limits = -1;
StanSOR *stan_ptr = NULL;

/* Globalna flaga ewakuacji - ustawiana przez handler sygnału */
volatile sig_atomic_t ewakuacja_trwa = 0;

/* Ile miejsc zajmuje pacjent (1 lub 2 z opiekunem) - używane jako kod wyjścia */
int sem_op_miejsca = 1;
int potrzebny_rodzic = 0;
pthread_t rodzic_thread;
unsigned long id_opiekuna = 0;

#define ETAP_POZA 0
#define ETAP_W_POCZEKALNI 1
#define ETAP_PO_REJESTRACJI 2

volatile int etap_pacjenta = ETAP_POZA;

/* Handler sygnału ewakuacji - tylko ustawia flagę */
void handle_ewakuacja(int sig)
{
    if (sig == SIG_EWAKUACJA) {
        ewakuacja_trwa = 1;
    }
}

/* Funkcja czyszcząca wywoływana przy ewakuacji 
 * Pacjent przekazuje informację o zajętych miejscach przez kod wyjścia _exit() */
void wykonaj_ewakuacje(void)
{
    /* Dołączamy wątek opiekuna jeśli istnieje */
    if (potrzebny_rodzic) {
        pthread_join(rodzic_thread, NULL);
    }
    
    /* _exit zamiast exit - bezpieczniejsze w kontekście sygnałów
     * Kod wyjścia = liczba zajętych miejsc (1 lub 2)
     * Generator zbierze te wartości przez WEXITSTATUS */
    _exit(sem_op_miejsca);
}

void* watek_rodzic(void* arg)
{
    /* Wątek opiekuna - może czekać na flagę ewakuacji */
    while (!ewakuacja_trwa) {
        usleep(100000);
    }
    return NULL;
}

void lock(int sem_indeks)
{
    struct sembuf operacja = {sem_indeks, -1, SEM_UNDO};
    
    while (semop(semid_limits, &operacja, 1) == -1) {
        if (errno == EINTR) {
            if (ewakuacja_trwa) {
                wykonaj_ewakuacje();
            }
            continue;
        }
        perror("lock: blad semop");
        _exit(sem_op_miejsca);
    }
}

void unlock(int sem_indeks)
{
    struct sembuf operacja = {sem_indeks, 1, SEM_UNDO};
    
    while (semop(semid_limits, &operacja, 1) == -1) {
        if (errno == EINTR) {
            continue; /* Przy unlock nie przerywamy */
        }
        perror("unlock: blad semop");
        break;
    }
}

int main(int argc, char *argv[])
{
    /* KRYTYCZNE: Ignorujemy SIGINT - tylko main go obsługuje */
    signal(SIGINT, SIG_IGN);
    
    /* Ustawiamy handler dla SIG_EWAKUACJA */
    struct sigaction sa;
    sa.sa_handler = handle_ewakuacja;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIG_EWAKUACJA, &sa, NULL);
    
    srand(time(NULL) ^ getpid());

    key_t key_sem = ftok(FILE_KEY, ID_SEM_SET);
    key_t key_limits = ftok(FILE_KEY, ID_SEM_LIMITS);
    key_t key_msg_rej = ftok(FILE_KEY, ID_KOLEJKA_REJESTRACJA);
    key_t key_msg_poz = ftok(FILE_KEY, ID_KOLEJKA_POZ);
    key_t key_shm = ftok(FILE_KEY, ID_SHM_MEM);

    semid = semget(key_sem, 0, 0);
    semid_limits = semget(key_limits, 0, 0);
    int rej_msgid = msgget(key_msg_rej, 0);
    int poz_id = msgget(key_msg_poz, 0);
    int shmid = shmget(key_shm, 0, 0);

    int spec_msgids[10];
    for (int i = 0; i < 10; i++) spec_msgids[i] = -1;
    spec_msgids[1] = msgget(ftok(FILE_KEY, ID_KOL_KARDIOLOG), 0);
    spec_msgids[2] = msgget(ftok(FILE_KEY, ID_KOL_NEUROLOG), 0);
    spec_msgids[3] = msgget(ftok(FILE_KEY, ID_KOL_LARYNGOLOG), 0);
    spec_msgids[4] = msgget(ftok(FILE_KEY, ID_KOL_CHIRURG), 0);
    spec_msgids[5] = msgget(ftok(FILE_KEY, ID_KOL_OKULISTA), 0);
    spec_msgids[6] = msgget(ftok(FILE_KEY, ID_KOL_PEDIATRA), 0);

    if (semid == -1 || rej_msgid == -1 || shmid == -1 || poz_id == -1 || semid_limits == -1) {
        perror("blad przy podlaczaniu do ipc");
        exit(EXIT_FAILURE);
    }

    for (int i = 1; i <= 6; i++) {
        if (spec_msgids[i] == -1) {
            perror("blad msgid dla specjalisty");
            exit(EXIT_FAILURE);
        }
    }

    StanSOR *stan = (StanSOR*)shmat(shmid, NULL, 0);
    if (stan == (void*)-1) {
        perror("blad shmat");
        exit(EXIT_FAILURE);
    }
    stan_ptr = stan;

    pid_t mpid = getpid();
    int wiek = rand() % 100;
    int vip = rand() % 100 < 20;

    potrzebny_rodzic = (wiek < 18);
    sem_op_miejsca = potrzebny_rodzic ? 2 : 1;

    if (potrzebny_rodzic) {
        if (pthread_create(&rodzic_thread, NULL, watek_rodzic, NULL) != 0) {
            perror("blad tworzenia watku rodzic");
            potrzebny_rodzic = 0;
            sem_op_miejsca = 1;
        } else {
            id_opiekuna = (unsigned long)rodzic_thread;
        }
    }

    char buf[150];
    if (potrzebny_rodzic) {
        sprintf(buf, "[pacjent] id: %d --- wiek: %d --- vip: %s\t [rodzic] tid: %lu\n",
                mpid, wiek, vip ? "tak" : "nie", id_opiekuna);
    } else {
        sprintf(buf, "[pacjent] id: %d --- wiek: %d --- vip: %s\n",
                mpid, wiek, vip ? "tak" : "nie");
    }
    zapisz_raport(KONSOLA, semid, buf);

    /* === WEJŚCIE DO POCZEKALNI === */
    struct sembuf wejscie_do_poczekalni = {SEM_MIEJSCA_SOR, -sem_op_miejsca, SEM_UNDO};

    while (semop(semid, &wejscie_do_poczekalni, 1) == -1) {
        if (errno == EINTR) {
            if (ewakuacja_trwa) {
                /* Pacjent jeszcze nie wszedł - wychodzi z kodem 0 (nie zajął miejsca) */
                if (potrzebny_rodzic) pthread_join(rodzic_thread, NULL);
                _exit(0);
            }
            continue;
        }
        perror("blad semop wejscie do poczekalni");
        _exit(0);
    }

    etap_pacjenta = ETAP_W_POCZEKALNI;

    /* Sprawdzamy ewakuację po wejściu */
    if (ewakuacja_trwa) {
        wykonaj_ewakuacje();
    }

    struct sembuf mutex_lock = {SEM_DOSTEP_PAMIEC, -1, SEM_UNDO};
    struct sembuf mutex_unlock = {SEM_DOSTEP_PAMIEC, 1, SEM_UNDO};

    /* Aktualizacja stanu - z obsługą EINTR */
    while (semop(semid, &mutex_lock, 1) == -1) {
        if (errno == EINTR) {
            if (ewakuacja_trwa) wykonaj_ewakuacje();
            continue;
        }
        perror("mutex lock"); break;
    }

    stan->liczba_pacjentow_w_srodku += sem_op_miejsca;
    stan->dlugosc_kolejki_rejestracji++;

    int prog_otwarcia = MAX_PACJENTOW / 2;
    if (!stan->czy_okienko_2_otwarte && stan->dlugosc_kolejki_rejestracji > prog_otwarcia) {
        stan->czy_okienko_2_otwarte = 1;
        zapisz_raport(KONSOLA, semid, "[REJESTRACJA] Otwieram okienko 2 (Kolejka: %d)\n", stan->dlugosc_kolejki_rejestracji);
        zapisz_raport(RAPORT_2, semid, "[REJESTRACJA] Otwieram 2 okienko | osob w kolejce: %d\n", stan->dlugosc_kolejki_rejestracji);
    }

    semop(semid, &mutex_unlock, 1);

    if (ewakuacja_trwa) wykonaj_ewakuacje();

    /* === REJESTRACJA === */
    KomunikatPacjenta msg;
    memset(&msg, 0, sizeof(msg));
    msg.mtype = vip ? TYP_VIP : TYP_ZWYKLY;
    msg.wiek = wiek;
    msg.pacjent_pid = mpid;
    msg.czy_vip = vip;

    lock(SLIMIT_REJESTRACJA);
    
    if (ewakuacja_trwa) {
        unlock(SLIMIT_REJESTRACJA);
        wykonaj_ewakuacje();
    }

    /* Wysyłanie do rejestracji z obsługą EINTR */
    while (msgsnd(rej_msgid, &msg, sizeof(KomunikatPacjenta) - sizeof(long), 0) == -1) {
        if (errno == EINTR) {
            if (ewakuacja_trwa) {
                unlock(SLIMIT_REJESTRACJA);
                wykonaj_ewakuacje();
            }
            continue;
        }
        if (errno == EAGAIN) { usleep(10000); continue; }
        perror("blad wysylania do rejestracji");
        _exit(sem_op_miejsca);
    }

    /* Odbieranie odpowiedzi z obsługą EINTR */
    while (msgrcv(rej_msgid, &msg, sizeof(KomunikatPacjenta) - sizeof(long), mpid, 0) == -1) {
        if (errno == EINTR) {
            if (ewakuacja_trwa) {
                unlock(SLIMIT_REJESTRACJA);
                wykonaj_ewakuacje();
            }
            continue;
        }
        perror("blad msgrcv od rejestracji");
        _exit(sem_op_miejsca);
    }
    unlock(SLIMIT_REJESTRACJA);

    /* Aktualizacja kolejki rejestracji */
    while (semop(semid, &mutex_lock, 1) == -1) {
        if (errno == EINTR) {
            if (ewakuacja_trwa) wykonaj_ewakuacje();
            continue;
        }
        break;
    }
    stan->dlugosc_kolejki_rejestracji--;
    semop(semid, &mutex_unlock, 1);

    etap_pacjenta = ETAP_PO_REJESTRACJI;

    if (ewakuacja_trwa) wykonaj_ewakuacje();

    /* === POZ === */
    msg.mtype = 1;
    lock(SLIMIT_POZ);
    
    if (ewakuacja_trwa) {
        unlock(SLIMIT_POZ);
        wykonaj_ewakuacje();
    }

    while (msgsnd(poz_id, &msg, sizeof(KomunikatPacjenta) - sizeof(long), 0) == -1) {
        if (errno == EINTR) {
            if (ewakuacja_trwa) {
                unlock(SLIMIT_POZ);
                wykonaj_ewakuacje();
            }
            continue;
        }
        if (errno == EAGAIN) { usleep(10000); continue; }
        perror("blad msgsnd wysylania do poz");
        _exit(sem_op_miejsca);
    }

    while (msgrcv(poz_id, &msg, sizeof(KomunikatPacjenta) - sizeof(long), mpid, 0) == -1) {
        if (errno == EINTR) {
            if (ewakuacja_trwa) {
                unlock(SLIMIT_POZ);
                wykonaj_ewakuacje();
            }
            continue;
        }
        perror("blad msgrcv od poz");
        _exit(sem_op_miejsca);
    }
    unlock(SLIMIT_POZ);

    if (ewakuacja_trwa) wykonaj_ewakuacje();

    /* === SPECJALISTA (jeśli potrzebny) === */
    if (msg.typ_lekarza > 0) {
        int id_spec = msg.typ_lekarza;
        msg.mtype = msg.kolor;
        int sem_limit_indeks = -1;

        switch (id_spec) {
            case LEK_KARDIOLOG:  sem_limit_indeks = SLIMIT_KARDIOLOG; break;
            case LEK_NEUROLOG:   sem_limit_indeks = SLIMIT_NEUROLOG; break;
            case LEK_LARYNGOLOG: sem_limit_indeks = SLIMIT_LARYNGOLOG; break;
            case LEK_CHIRURG:    sem_limit_indeks = SLIMIT_CHIRURG; break;
            case LEK_OKULISTA:   sem_limit_indeks = SLIMIT_OKULISTA; break;
            case LEK_PEDIATRA:   sem_limit_indeks = SLIMIT_PEDIATRA; break;
            default: break;
        }

        if (sem_limit_indeks != -1) {
            lock(sem_limit_indeks);
            
            if (ewakuacja_trwa) {
                unlock(sem_limit_indeks);
                wykonaj_ewakuacje();
            }

            while (msgsnd(spec_msgids[id_spec], &msg, sizeof(KomunikatPacjenta) - sizeof(long), 0) == -1) {
                if (errno == EINTR) {
                    if (ewakuacja_trwa) {
                        unlock(sem_limit_indeks);
                        wykonaj_ewakuacje();
                    }
                    continue;
                }
                if (errno == EAGAIN) { usleep(10000); continue; }
                perror("blad wysylania do specjalisty");
                _exit(sem_op_miejsca);
            }

            while (msgrcv(spec_msgids[id_spec], &msg, sizeof(KomunikatPacjenta) - sizeof(long), mpid, 0) == -1) {
                if (errno == EINTR) {
                    if (ewakuacja_trwa) {
                        unlock(sem_limit_indeks);
                        wykonaj_ewakuacje();
                    }
                    continue;
                }
                perror("blad msgrcv od specjalisty");
                _exit(sem_op_miejsca);
            }

            unlock(sem_limit_indeks);
        }
    }

    if (ewakuacja_trwa) wykonaj_ewakuacje();

    /* === WYJŚCIE - normalne zakończenie wizyty === */
    while (semop(semid, &mutex_lock, 1) == -1) {
        if (errno == EINTR) continue;
        break;
    }
    
    stan->liczba_pacjentow_w_srodku -= sem_op_miejsca;
    stan->obs_pacjenci++;
    if (msg.czy_vip) stan->ile_vip++;
    stan->obs_kolory[msg.kolor]++;
    if (!(msg.typ_lekarza)) {
        stan->obs_dom_poz++;
        stan->decyzja[msg.skierowanie]++;
    } else {
        stan->obs_spec[msg.typ_lekarza]++;
        stan->decyzja[msg.skierowanie]++;
    }
    
    semop(semid, &mutex_unlock, 1);

    if (potrzebny_rodzic) {
        pthread_join(rodzic_thread, NULL);
    }

    struct sembuf wyjscie_z_poczekalni = {SEM_MIEJSCA_SOR, sem_op_miejsca, SEM_UNDO};
    semop(semid, &wyjscie_z_poczekalni, 1);

    etap_pacjenta = ETAP_POZA;
    shmdt(stan);

    usleep(100000);

    /* Normalne wyjście - kod 0 bo pacjent już oddał miejsce */
    return 0;
}