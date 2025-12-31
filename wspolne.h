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

//parametry ftok

#define FILE_KEY "."

#define ID_MSG_QUEUE 'Q'
#define ID_SHM_MEM 'M'
#define ID_SEM_SET 'S'


#define MAX_PACJENTOW 20 //N
#define LIMIT_KOLEJKI_K 10 //K-prog otwracia drugiej


//priorytety dla mtype

#define CZERWONY 1
#define ZOLTY 2
#define ZZIELONY 3


#define KARDIOLOG 1
#define NEUROLOG 2
#define OKULISTA 3

#define SIG_LEKARZ_ODDZIAL SIGUSR1
#define SIG_EWAKUACJA SIGUSR2



#endif