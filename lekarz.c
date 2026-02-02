#define _GNU_SOURCE
#include "wspolne.h"

int semid = -1;
int shmid = -1;
int typ_lekarza = 0;    
volatile sig_atomic_t wezwanie_na_oddzial = 0;
volatile sig_atomic_t koniec_pracy = 0;

void handle_sig(int sig)
{
    if(sig == SIG_LEKARZ_ODDZIAL) wezwanie_na_oddzial = 1;
    else if(sig == SIGINT || sig == SIGTERM) koniec_pracy = 1;
}

const char* int_to_lekarz(int typ) {
    const char* nazwy[] = {"POZ", "Kardiolog", "Neurolog", "Laryngolog", "Chirurg", "Okulista", "Pediatra"};
    if (typ >= 0 && typ <= 6) return nazwy[typ];
    return "Lekarz";
}

void praca_poz(int msgid_poz)
{
    KomunikatPacjenta pacjent;   
    while(!koniec_pracy)
    {
        if(msgrcv(msgid_poz, &pacjent, sizeof(pacjent) - sizeof(long), 0, IPC_NOWAIT) == -1) {
            if (errno == ENOMSG || errno == EINTR) { usleep(50000); continue; }
            break;
        } 
        
        int r = rand() % 100;
        if (r < 10) pacjent.kolor = CZERWONY;
        else if (r < 45) pacjent.kolor = ZOLTY;
        else if (r < 95) pacjent.kolor = ZIELONY;
        else {
            pacjent.typ_lekarza = 0;
            pacjent.skierowanie = 1;
            pacjent.kolor = 0;
        }

        if (pacjent.kolor != 0) {
            if (pacjent.wiek < 18) pacjent.typ_lekarza = LEK_PEDIATRA;
            else pacjent.typ_lekarza = (rand() % 5) + 1;
        }

        pacjent.mtype = pacjent.pacjent_pid;
        if(koniec_pracy) break;
        
        // LOGOWANIE POZ NA KONSOLĘ
        zapisz_raport(KONSOLA, semid, "[POZ] Pacjent %d -> %s (Kolor: %d)\n", 
                     pacjent.pacjent_pid, int_to_lekarz(pacjent.typ_lekarza), pacjent.kolor);

        msgsnd(msgid_poz, &pacjent, sizeof(pacjent) - sizeof(long), 0);
    }
}

void praca_specjalista(int typ, int msgid)
{
    StanSOR *stan = (StanSOR*)shmat(shmid, NULL, 0);
    if (stan == (void*)-1) exit(1);

    struct sembuf lock = {SEM_DOSTEP_PAMIEC, -1, SEM_UNDO};
    struct sembuf unlock = {SEM_DOSTEP_PAMIEC, 1, SEM_UNDO};
    KomunikatPacjenta pacjent;

    while(!koniec_pracy)
    {
        if(wezwanie_na_oddzial) {
            while(semop(semid, &lock, 1) == -1) { if(errno!=EINTR) break; }
            stan->dostepni_specjalisci[typ] = 0;
            while(semop(semid, &unlock, 1) == -1) { if(errno!=EINTR) break; }
            
            zapisz_raport(KONSOLA, semid, "[%s] Wezwanie na oddzial\n", int_to_lekarz(typ));
            
            for(int i=0; i<30; i++) {
                if(koniec_pracy) break;
                usleep(100000);
            }

            while(semop(semid, &lock, 1) == -1) { if(errno!=EINTR) break; }
            stan->dostepni_specjalisci[typ] = 1;
            while(semop(semid, &unlock, 1) == -1) { if(errno!=EINTR) break; }
            
            wezwanie_na_oddzial = 0;
        }

        if(msgrcv(msgid, &pacjent, sizeof(pacjent) - sizeof(long), -3, IPC_NOWAIT) == -1) {
            if (errno == ENOMSG || errno == EINTR) { usleep(50000); continue; }
            break;            
        }

        int r = rand() % 1000;
        if (r < 850) pacjent.skierowanie = 1;
        else if (r < 995) pacjent.skierowanie = 2;
        else pacjent.skierowanie = 3;

        zapisz_raport(KONSOLA, semid, "[%s] Badanie pacjenta %d\n", int_to_lekarz(typ), pacjent.pacjent_pid);

        pacjent.mtype = pacjent.pacjent_pid;
        msgsnd(msgid, &pacjent, sizeof(pacjent) - sizeof(long), 0);
    }
    shmdt(stan);
}

int main(int argc, char*argv[])
{
    struct sigaction sa;
    sa.sa_handler = handle_sig;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIG_LEKARZ_ODDZIAL, &sa, NULL);
    sigaction(SIGINT, &sa, NULL); 

    signal(SIGTERM, handle_sig);
    
    if (argc < 2) exit(1);
    typ_lekarza = atoi(argv[1]);

    int msgid_poz = msgget(ftok(FILE_KEY, ID_KOLEJKA_POZ), 0);
    semid = semget(ftok(FILE_KEY, ID_SEM_SET), 0, 0); 
    shmid = shmget(ftok(FILE_KEY, ID_SHM_MEM), 0, 0);

    srand(time(NULL) ^ getpid());

    if (typ_lekarza == 0) praca_poz(msgid_poz);
    else {
        int symbol = (typ_lekarza==1?'K':typ_lekarza==2?'N':typ_lekarza==3?'L':typ_lekarza==4?'C':typ_lekarza==5?'O':'D');
        int mid = msgget(ftok(FILE_KEY, symbol), 0);
        praca_specjalista(typ_lekarza, mid);
    }
    
    zapisz_raport(KONSOLA, semid, "[%s] Koniec pracy.\n", int_to_lekarz(typ_lekarza));
    return 0;
}