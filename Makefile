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
	ipcs -m | grep `whoami` | awk '{print $$2}' | xargs -r ipcrm -m || true
	ipcs -s | grep `whoami` | awk '{print $$2}' | xargs -r ipcrm -s || true
	ipcs -q | grep `whoami` | awk '{print $$2}' | xargs -r ipcrm -q || true