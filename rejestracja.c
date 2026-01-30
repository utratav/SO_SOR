#define _DEFAULT_SOURCE
#include "wspolne.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

int nr_okienka;
int semid = -1;

volatile sig_atomic_t ewakuacja_trwa = 0;

static void handle_sig(int sig)
{
    if (sig == SIG_EWAKUACJA) {
        ewakuacja_trwa = 1;
    }
}

static int semop_evac(int semid, struct sembuf *op)
{
    for (;;) {
        if (semop(semid, op, 1) == 0) return 0;
        if (errno == EINTR) {
            if (ewakuacja_trwa) return -2;
            continue;
        }
        return -1;
    }
}

static int msgsnd_reply_nowait_retry(int msgid, KomunikatPacjenta *p)
{
    for (;;) {
        if (msgsnd(msgid, p, sizeof(KomunikatPacjenta) - sizeof(long), IPC_NOWAIT) == 0)
            return 0;

        if (errno == EINTR) {
            if (ewakuacja_trwa) return -2;
            continue;
        }
        if (errno == EAGAIN) { usleep(1000); continue; }
        return -1;
    }
}

int main(int argc, char *argv[])
{
    signal(SIGINT, SIG_IGN);

    struct sigaction sa;
    sa.sa_handler = handle_sig;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIG_EWAKUACJA, &sa, NULL);

    if (argc < 2) {
        fprintf(stderr, "niepoprawnie uzyta skladnia. uzyj: %s (int)nr_okienka\n", argv[0]);
        exit(1);
    }

    nr_okienka = atoi(argv[1]);

    int shmid   = shmget(ftok(FILE_KEY, ID_SHM_MEM), 0, 0);
    semid       = semget(ftok(FILE_KEY, ID_SEM_SET), 0, 0);
    int msgid_we = msgget(ftok(FILE_KEY, ID_KOLEJKA_REJESTRACJA), 0);

    if (shmid == -1 || semid == -1 || msgid_we == -1) {
        perror("rejestracja - Blad polaczenia IPC");
        exit(1);
    }

    StanSOR *stan = (StanSOR*)shmat(shmid, NULL, 0);
    if (stan == (void*)-1) {
        perror("rejestracja - blad shmat");
        exit(1);
    }

    struct sembuf shm_lock   = {SEM_DOSTEP_PAMIEC, -1, SEM_UNDO};
    struct sembuf shm_unlock = {SEM_DOSTEP_PAMIEC,  1, SEM_UNDO};

    struct sembuf czekaj_na_prace = {SEM_BRAMKA_2, -1, 0};
    struct sembuf oddaj_prace     = {SEM_BRAMKA_2,  1, 0};

    KomunikatPacjenta pacjent;
    int lokalnie_otwarte = 0;

    int gate_held = 0;
    int mask_blocked = 0;
    sigset_t oldmask;

    while (!ewakuacja_trwa) {

        
        if (nr_okienka == 1) {
            int do_open = 0;
            int do_close = 0;
            int dlugosc_kolejki = 0;

            if (semop_evac(semid, &shm_lock) == -2) break;
            if (errno && errno != 0 && errno != EINTR) {
            }

            int wymagane_otwarcie = stan->czy_okienko_2_otwarte;
            dlugosc_kolejki = stan->dlugosc_kolejki_rejestracji;

            if (wymagane_otwarcie && !lokalnie_otwarte) {
                lokalnie_otwarte = 1;
                do_open = 1;
            }

            if (lokalnie_otwarte && dlugosc_kolejki <= (MAX_PACJENTOW / 3)) {
                stan->czy_okienko_2_otwarte = 0;
                lokalnie_otwarte = 0;
                do_close = 1;
            }

            semop(semid, &shm_unlock, 1);

            if (do_open) {
                semop(semid, &oddaj_prace, 1);
                zapisz_raport(KONSOLA, semid, "[REJESTRACJA] Otwieram okienko 2.\n");
            }

            if (do_close) {
                
                sigset_t m;
                block_sigtstp(&m);

                int rc = semop_evac(semid, &czekaj_na_prace);
                restore_sigmask(&m);

                if (rc == -2) break;            // ewakuacja
                if (rc == -1) {
                    perror("rejestracja 1 - blad zamkniecia bramki");
                } else {
                    zapisz_raport(KONSOLA, semid, "[REJESTRACJA] Zamykam okienko 2 (Kolejka: %d)\n", dlugosc_kolejki);
                    zapisz_raport(RAPORT_2, semid, "[REJESTRACJA] Zamykam 2 okienko | osob w kolejce: %d\n", dlugosc_kolejki);
                }
            }
        }


        if (nr_okienka == 2) {
            int rc = semop_evac(semid, &czekaj_na_prace);
            if (rc == -2) break;
            if (rc == -1) {
                perror("rejestracja 2 - blad semop (czekaj)");
                break;
            }

            gate_held = 1;
            block_sigtstp(&oldmask);
            mask_blocked = 1;
        }

        if (ewakuacja_trwa) {
            if (nr_okienka == 2 && gate_held) {
                semop(semid, &oddaj_prace, 1);
                gate_held = 0;
            }
            if (mask_blocked) {
                restore_sigmask(&oldmask);
                mask_blocked = 0;
            }
            break;
        }

        int flaga_rcv = (nr_okienka == 2) ? IPC_NOWAIT : 0;

        ssize_t status = msgrcv(msgid_we, &pacjent, sizeof(KomunikatPacjenta) - sizeof(long), -2, flaga_rcv);
        if (status == -1) {

            if (nr_okienka == 2 && errno == ENOMSG) {
                if (gate_held) { semop(semid, &oddaj_prace, 1); gate_held = 0; }
                if (mask_blocked) { restore_sigmask(&oldmask); mask_blocked = 0; }
                usleep(10000);
                continue;
            }

            if (errno == EINTR) {
                if (ewakuacja_trwa) {
                    if (nr_okienka == 2 && gate_held) { semop(semid, &oddaj_prace, 1); gate_held = 0; }
                    if (mask_blocked) { restore_sigmask(&oldmask); mask_blocked = 0; }
                    break;
                }
                if (nr_okienka == 2 && gate_held) { semop(semid, &oddaj_prace, 1); gate_held = 0; }
                if (mask_blocked) { restore_sigmask(&oldmask); mask_blocked = 0; }
                continue;
            }

            perror("rejestracja - blad msgrcv");
            if (nr_okienka == 2 && gate_held) { semop(semid, &oddaj_prace, 1); gate_held = 0; }
            if (mask_blocked) { restore_sigmask(&oldmask); mask_blocked = 0; }
            continue;
        }

        if (ewakuacja_trwa) {
            if (nr_okienka == 2 && gate_held) { semop(semid, &oddaj_prace, 1); gate_held = 0; }
            if (mask_blocked) { restore_sigmask(&oldmask); mask_blocked = 0; }
            break;
        }

        pacjent.mtype = pacjent.pacjent_pid;

        int snd = msgsnd_reply_nowait_retry(msgid_we, &pacjent);
        if (snd == -2) {
            if (nr_okienka == 2 && gate_held) { semop(semid, &oddaj_prace, 1); gate_held = 0; }
            if (mask_blocked) { restore_sigmask(&oldmask); mask_blocked = 0; }
            break;
        }
        if (snd == -1) {
            perror("rejestracja - blad wyslania reply");
        }

        zapisz_raport(KONSOLA, semid,
            "[rejestracja] okienko %d: pacjent %d zarejestrowany\n",
            nr_okienka, pacjent.pacjent_pid);

        if (nr_okienka == 2 && gate_held) {
            semop(semid, &oddaj_prace, 1);
            gate_held = 0;
        }
        if (mask_blocked) {
            restore_sigmask(&oldmask);
            mask_blocked = 0;
        }
    }

    if (nr_okienka == 2 && gate_held) {
        semop(semid, &oddaj_prace, 1);
        gate_held = 0;
    }
    if (mask_blocked) {
        restore_sigmask(&oldmask);
        mask_blocked = 0;
    }

    zapisz_raport(KONSOLA, semid, "[rejestracja %d] Otrzymano ewakuacje. Zamykam okienko.\n", nr_okienka);
    shmdt(stan);
    return 0;
}
