
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

void handle_sig(int sig)
{
    if (sig == SIG_EWAKUACJA) {
        ewakuacja_trwa = 1;
    }
}

int main(int argc, char *argv[])
{
    /* KRYTYCZNE: Ignorujemy SIGINT */
    signal(SIGINT, SIG_IGN);

    /* Handler dla ewakuacji */
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

    int shmid = shmget(ftok(FILE_KEY, ID_SHM_MEM), 0, 0);
    semid = semget(ftok(FILE_KEY, ID_SEM_SET), 0, 0);
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

    struct sembuf lock = {SEM_DOSTEP_PAMIEC, -1, SEM_UNDO};
    struct sembuf unlock = {SEM_DOSTEP_PAMIEC, 1, SEM_UNDO};
    struct sembuf czekaj_na_prace = {SEM_BRAMKA_2, -1, 0};
    struct sembuf oddaj_prace = {SEM_BRAMKA_2, 1, 0};

    KomunikatPacjenta pacjent;
    int lokalnie_otwarte = 0;

    while (!ewakuacja_trwa) {

        if (ewakuacja_trwa) break;

        /* Okienko 1 - zarządza bramką */
        if (nr_okienka == 1) {
            while (semop(semid, &lock, 1) == -1) {
                if (errno == EINTR) {
                    if (ewakuacja_trwa) goto koniec;
                    continue;
                }
                perror("rejestracja 1 - blad lock");
                break;
            }

            int wymagane_otwarcie = stan->czy_okienko_2_otwarte;
            int dlugosc_kolejki = stan->dlugosc_kolejki_rejestracji;

            if (wymagane_otwarcie && !lokalnie_otwarte) {
                lokalnie_otwarte = 1;
                semop(semid, &oddaj_prace, 1);
                zapisz_raport(KONSOLA, semid, "[REJESTRACJA] Wykryto flage od pacjenta -> Otwieram okienko 2.\n");
            }

            if (lokalnie_otwarte && dlugosc_kolejki <= (MAX_PACJENTOW / 3)) {
                stan->czy_okienko_2_otwarte = 0;
                lokalnie_otwarte = 0;

                while (semop(semid, &czekaj_na_prace, 1) == -1) {
                    if (errno == EINTR) {
                        if (ewakuacja_trwa) {
                            semop(semid, &unlock, 1);
                            goto koniec;
                        }
                        continue;
                    }
                    break;
                }

                zapisz_raport(KONSOLA, semid, "[REJESTRACJA] Zamykam okienko 2 (Kolejka: %d)\n", dlugosc_kolejki);
                zapisz_raport(RAPORT_2, semid, "[REJESTRACJA] Zamykam 2 okienko | osob w kolejce: %d\n", dlugosc_kolejki);
            }

            semop(semid, &unlock, 1);
        }

        /* Okienko 2 - czeka na bramkę */
        if (nr_okienka == 2) {
            while (semop(semid, &czekaj_na_prace, 1) == -1) {
                if (errno == EINTR) {
                    if (ewakuacja_trwa) goto koniec;
                    continue;
                }
                perror("rejestracja 2 - blad semop (czekaj)");
                goto koniec;
            }
        }

        if (ewakuacja_trwa) {
            if (nr_okienka == 2) semop(semid, &oddaj_prace, 1);
            break;
        }

        int flaga = (nr_okienka == 2) ? IPC_NOWAIT : 0;

        ssize_t status = msgrcv(msgid_we, &pacjent, sizeof(KomunikatPacjenta) - sizeof(long), -2, flaga);

        if (status == -1) {
            if (nr_okienka == 2 && errno == ENOMSG) {
                semop(semid, &oddaj_prace, 1);
                usleep(10000);
                continue;
            }

            if (errno == EINTR) {
                if (ewakuacja_trwa) {
                    if (nr_okienka == 2) semop(semid, &oddaj_prace, 1);
                    break;
                }
                if (nr_okienka == 2) semop(semid, &oddaj_prace, 1);
                continue;
            }

            perror("rejestracja - blad msgrcv");
            if (nr_okienka == 2) semop(semid, &oddaj_prace, 1);
            continue;
        }

        if (ewakuacja_trwa) {
            if (nr_okienka == 2) semop(semid, &oddaj_prace, 1);
            break;
        }

        pacjent.mtype = pacjent.pacjent_pid;

        while (msgsnd(msgid_we, &pacjent, sizeof(KomunikatPacjenta) - sizeof(long), 0) == -1) {
            if (errno == EINTR) {
                if (ewakuacja_trwa) {
                    if (nr_okienka == 2) semop(semid, &oddaj_prace, 1);
                    goto koniec;
                }
                continue;
            }
            perror("rejestracja - blad przekazania do POZ");
            break;
        }

        zapisz_raport(KONSOLA, semid, "[rejestracja] okienko %d: pacjent %d przekazany do POZ\n",
            nr_okienka, pacjent.pacjent_pid);

        if (nr_okienka == 2) semop(semid, &oddaj_prace, 1);
    }

koniec:
    zapisz_raport(KONSOLA, semid, "[rejestracja %d] Otrzymano ewakuacje. Zamykam okienko.\n", nr_okienka);
    shmdt(stan);
    return 0;
}