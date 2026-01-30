#define _DEFAULT_SOURCE
#include "wspolne.h"

int semid = -1;
int shmid = -1;

volatile sig_atomic_t ewakuacja_trwa = 0;
volatile sig_atomic_t chld_flag = 0;

#define MAX_DZIECI 60000
pid_t pidy_dzieci[MAX_DZIECI];
int liczba_dzieci = 0;

static void handle_sigchld(int sig)
{
    (void)sig;
    chld_flag = 1;
}

static void handle_ewakuacja(int sig)
{
    if (sig == SIG_EWAKUACJA) {
        ewakuacja_trwa = 1;
    }
}

static void reap_children_nonblock(void)
{
    int status;
    pid_t pid;

    for (;;) {
        pid = waitpid(-1, &status, WNOHANG);
        if (pid > 0) {
            struct sembuf v = {SEM_GENERATOR, 1, 0};
            (void)semop(semid, &v, 1);
            continue;
        }
        if (pid == 0) return;               
        if (pid == -1) {
            if (errno == EINTR) continue;
            return;
        }
    }
}

int main(int argc, char* argv[])
{
    (void)argc; (void)argv;

    signal(SIGINT, SIG_IGN);

    struct sigaction sa_chld;
    sa_chld.sa_handler = handle_sigchld;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa_chld, NULL);

    struct sigaction sa_ewak;
    sa_ewak.sa_handler = handle_ewakuacja;
    sigemptyset(&sa_ewak.sa_mask);
    sa_ewak.sa_flags = 0;
    sigaction(SIG_EWAKUACJA, &sa_ewak, NULL);

    key_t key_sem = ftok(FILE_KEY, ID_SEM_SET);
    key_t key_shm = ftok(FILE_KEY, ID_SHM_MEM);

    semid = semget(key_sem, 0, 0);
    shmid = shmget(key_shm, 0, 0);

    if (semid == -1) {
        perror("generator: blad semget");
        exit(1);
    }
    if (shmid == -1) {
        perror("generator: blad shmget");
        exit(1);
    }

    srand(time(NULL) ^ getpid());

    zapisz_raport(KONSOLA, semid,
        "\n[GENERATOR] Start symulacji. Cel: %d pacjentow (24h). Rownolegle max: %d\n",
        PACJENCI_NA_DOBE, MAX_PROCESOW);

    int wygenerowani = 0;

    for (int i = 0; i < PACJENCI_NA_DOBE && !ewakuacja_trwa; i++) {

        if (chld_flag) {
            chld_flag = 0;
            reap_children_nonblock();
        }

        struct sembuf p = {SEM_GENERATOR, -1, 0};
        while (semop(semid, &p, 1) == -1) {
            if (errno == EINTR) {
                if (ewakuacja_trwa) goto koniec_generowania;
                if (chld_flag) {
                    chld_flag = 0;
                    reap_children_nonblock();
                }
                continue;
            }
            perror("generator: semop P(SEM_GENERATOR) error");
            goto koniec_generowania;
        }

        if (ewakuacja_trwa) {
            struct sembuf v = {SEM_GENERATOR, 1, 0};
            semop(semid, &v, 1);
            break;
        }

        pid_t pid = fork();
        if (pid == 0) {
            execl("./pacjent", "pacjent", NULL);
            perror("generator: execl failed");
            _exit(1);
        } else if (pid == -1) {
            perror("generator: fork failed");
            struct sembuf v = {SEM_GENERATOR, 1, 0};
            semop(semid, &v, 1);
            usleep(10000);
            i--;
            continue;
        } else {
            if (liczba_dzieci < MAX_DZIECI) {
                pidy_dzieci[liczba_dzieci++] = pid;
            }
            wygenerowani++;
        }
    }

koniec_generowania:

    if (ewakuacja_trwa) {
        zapisz_raport(KONSOLA, semid,
            "[GENERATOR] Otrzymano sygnal ewakuacji. Wysylam do %d dzieci...\n",
            liczba_dzieci);

        for (int i = 0; i < liczba_dzieci; i++) {
            if (pidy_dzieci[i] > 0) {
                kill(pidy_dzieci[i], SIG_EWAKUACJA);
            }
        }
    } else {
        zapisz_raport(KONSOLA, semid,
            "[GENERATOR] Wygenerowano %d/%d pacjentow. Czekam na zakonczenie wszystkich...\n",
            wygenerowani, PACJENCI_NA_DOBE);
    }

    reap_children_nonblock();

    int suma_wag_ewakuowanych = 0;
    int liczba_ewakuowanych = 0;
    int status;
    pid_t child_pid;

    while ((child_pid = wait(&status)) > 0) {
        struct sembuf v = {SEM_GENERATOR, 1, 0};
        semop(semid, &v, 1);

        if (WIFEXITED(status)) {
            int kod_wyjscia = WEXITSTATUS(status);
            if (kod_wyjscia > 0) {
                suma_wag_ewakuowanych += kod_wyjscia;
                liczba_ewakuowanych++;
            }
        } else if (WIFSIGNALED(status)) {
            suma_wag_ewakuowanych += 1;
            liczba_ewakuowanych++;
        }
    }

    if (ewakuacja_trwa) {
        zapisz_raport(KONSOLA, semid,
            "[GENERATOR] Zebrano %d ewakuowanych procesow (waga: %d miejsc)\n",
            liczba_ewakuowanych, suma_wag_ewakuowanych);
    }

    if (ewakuacja_trwa) {
        StanSOR *stan = (StanSOR*)shmat(shmid, NULL, 0);
        if (stan != (void*)-1) {
            struct sembuf lock = {SEM_DOSTEP_PAMIEC, -1, SEM_UNDO};
            struct sembuf unlock = {SEM_DOSTEP_PAMIEC, 1, SEM_UNDO};

            while (semop(semid, &lock, 1) == -1) {
                if (errno == EINTR) continue;
                break;
            }

            stan->ewakuowani += suma_wag_ewakuowanych;
            stan->liczba_pacjentow_w_srodku -= suma_wag_ewakuowanych;
            if (stan->liczba_pacjentow_w_srodku < 0) stan->liczba_pacjentow_w_srodku = 0;

            semop(semid, &unlock, 1);

            zapisz_raport(KONSOLA, semid,
                "[GENERATOR] Zaktualizowano stan: ewakuowano %d miejsc\n",
                suma_wag_ewakuowanych);

            shmdt(stan);
        }
    }

    zapisz_raport(KONSOLA, semid, "[GENERATOR] Koniec pracy generatora.\n");
    return 0;
}
