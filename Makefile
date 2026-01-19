CC = gcc
CFLAGS = -Wall -pthread

TARGETS = main pacjent lekarz rejestracja generator


all: $(TARGETS)



main: main.c wspolne.h
	$(CC) $(CFLAGS) -o main main.c

pacjent: pacjent.c wspolne.h
	$(CC) $(CFLAGS) -o pacjent pacjent.c -lpthread

lekarz: lekarz.c wspolne.h
	$(CC) $(CFLAGS) -o lekarz lekarz.c

rejestracja: rejestracja.c wspolne.h
	$(CC) $(CFLAGS) -o rejestracja rejestracja.c


generator: generuj.c wspolne.h
	$(CC) $(CFLAGS) -o generator generuj.c


clean:
	rm -f $(TARGETS) *.o *.txt


ipc_clean:
	ipcrm -a