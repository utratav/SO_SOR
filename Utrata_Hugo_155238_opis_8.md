# Projekt Systemy Operacyjne – Temat 8: SOR

**Autor:** Hugo Utrata
**Numer albumu:** 155238
**Temat:** 8 – SOR

---

## Krótki opis projektu

Projekt realizuje symulację działania Szpitalnego Oddziału Ratunkowego (SOR) z uwzględnieniem rzeczywistych zasad organizacji pracy. System odtwarza przebieg wizyty pacjenta: wejście na SOR (z limitem miejsc), rejestrację, ocenę stanu zdrowia w triażu oraz konsultację u właściwego lekarza specjalisty. Pacjenci mają różne priorytety (czerwony, żółty, zielony) wpływające na kolejność obsługi, a niektórzy mogą być wysyłani do domu już na etapie triażu. Lekarze mogą kierować pacjentów do dalszego leczenia, wypisywać ich do domu lub odsyłać do innej placówki. Symulacja obejmuje również obsługę pacjentów VIP oraz reakcję całego systemu na sygnały Dyrektora (np. przerwanie pracy lekarza, natychmiastowa ewakuacja SOR). Program opiera się na procesach, użyto w nim rozmaite systemy IPC, o których później.

---

## Wytyczne projektu – Temat 8: SOR (Kopia 1:1 PDF)

SOR działa przez całą dobę, zapewniając natychmiastową pomoc osobom w stanach nagłego zagrożenia zdrowia i życia. Działanie SOR-u opiera się na triażu, czyli ocenie stanu pacjentów, który określa priorytet udzielania pomocy (nie decyduje kolejność zgłoszenia). W poczekalni jest N miejsc.

### Zasady działania SOR:

* SOR jest czynny całą dobę;
* W poczekalni SOR w danej chwili może się znajdować co najwyżej N pacjentów (pozostali, jeżeli są czekają przed wejściem);
* Dzieci w wieku poniżej 18 lat na SOR przychodzą pod opieką osoby dorosłej;
* Osoby uprawnione VIP (np. honorowy dawca krwi,) do rejestracji podchodzą bez kolejki;
* Każdy pacjent przed wizytą u lekarza musi się udać do rejestracji;
* W przychodni są 2 okienka rejestracji, zawsze działa min. 1 stanowisko;
* Jeżeli w kolejce do rejestracji stoi więcej niż K pacjentów (K>=N/2) otwiera się drugie okienko rejestracji. Drugie okienko zamyka się jeżeli liczba pacjentów w kolejce do rejestracji jest mniejsza niż N/3;

### Przebieg wizyty na SOR:

#### 1. Rejestracja:

− Pacjent podaje swoje dane i opisuje objawy.

#### 2. Ocena stanu zdrowia (Triaż):

− Lekarz POZ weryfikuje stan pacjenta i przypisuje mu kolor zgodny z pilnością udzielenia pomocy (na tej podstawie określa się, kto otrzyma pomoc w pierwszej kolejności):

* **czerwony** – natychmiastowa pomoc – ok. 10% pacjentów;
* **żółty** – przypadek pilny – ok. 35% pacjentów;
* **zielony** – przypadek stabilny – ok. 50% pacjentów;
  − Ok. 5% pacjentów jest odsyłanych do domu bezpośrednio z triażu;
  − Lekarz POZ po przypisaniu koloru, kieruje danego pacjenta do lekarza specjalisty: kardiologa, neurologa, okulisty, laryngologa, chirurga, pediatry.

#### 3. Wstępna diagnostyka i leczenie:

− Lekarz specjalista wykonuje niezbędne badania (wywiad, badanie fizykalne, EKG, pomiar ciśnienia, …), aby ustabilizować funkcje życiowe pacjenta.

#### 4. Decyzja o dalszym postępowaniu:

− Po wstępnej diagnozie i stabilizacji stanu pacjent może zostać przez lekarza specjalistę:

* wypisany do domu – ok. 85% pacjentów;
* skierowany na dalsze leczenie do odpowiedniego oddziału szpitalnego – ok. 14.5% pacjentów;
* skierowany do innej, specjalistycznej placówki – ok. 0,5% pacjentów.

### Sygnały Dyrektora:

* **Sygnał 1** – dany lekarz specjalista bada bieżącego pacjenta i przerywa pracę na SOR-rze i udaje się na oddział. Wraca po określonym losowo czasie.
* **Sygnał 2** – wszyscy pacjenci i lekarze natychmiast opuszczają budynek.







---

## ARCHITEKTURA IPC

### A. Tworzenie i zarządzanie procesami (Generator Pacjentów)

Generator pacjentów (`generuj.c`) jest odpowiedzialny za dynamiczne tworzenie procesów pacjentów w kontrolowany sposób, zapobiegając jednocześnie przeciążeniu system przez fork - bomby.

#### A.1. Ograniczanie liczby jednoczesnych procesów

Przed każdym wywołaniem `fork()`, generator wykonuje operację P na semaforze `SEM_GENERATOR`, który jest inicjalizowany wartością `MAX_PROCESOW` (wartość wedle uznania, ogranicza nas systemowy max dla semafora). Jeśli limit jest wyczerpany, proces generatora blokuje się do momentu zwolnienia miejsca.

[forkowanie i semop: generuj.c](https://github.com/utratav/SO_SOR/blob/c636695b4c763e3f9b6acc920a17a64fc873c2d2/generuj.c#L91-L115)



Flaga `SEM_UNDO` zapewnia automatyczne zwolnienie semafora w przypadku nieoczekiwanego zakończenia procesu generatora.

#### A.2. Obsługa zakończenia procesów potomnych (SIGCHLD)

Kiedy proces pacjenta kończy działanie (przez `exit()` lub sygnał), jądro wysyła sygnał `SIGCHLD` do procesu rodzica (generatora). Handler ustawia flagę `sigchld_received`, a właściwa obsługa odbywa się w funkcji `zbierz_zombie()`:

[zbieranie zombie: generuj.c](https://github.com/utratav/SO_SOR/blob/c636695b4c763e3f9b6acc920a17a64fc873c2d2/generuj.c#L6-L22)


Funkcja `waitpid(-1, &status, WNOHANG)` zbiera statusy zakończonych dzieci bez blokowania. Dla każdego zebranego procesu, semafor `SEM_GENERATOR` jest podnoszony (+1), zwalniając miejsce dla nowego pacjenta.

#### A.3. Konfiguracja sigaction

Struktura `sigaction` jest konfigurowana z kluczowymi flagami:

[struktura sigaction dla SIGCHLD: generuj.c](https://github.com/utratav/SO_SOR/blob/c636695b4c763e3f9b6acc920a17a64fc873c2d2/generuj.c#L67-L75)



* **`SA_RESTART`**: Po obsłużeniu sygnału `SIGCHLD`, przerwane wywołanie systemowe (np. `semop`) zostanie automatycznie wznowione zamiast zwracać błąd `EINTR`.
* **`SA_NOCLDSTOP`**: Sygnał `SIGCHLD` będzie wysyłany tylko przy faktycznym zakończeniu procesu potomnego, nie przy jego zatrzymaniu (`SIGSTOP`).

#### A.4. Podmiana obrazu procesu (execl)

Po udanym `fork()`, proces potomny natychmiast podmienia swój obraz na program pacjenta za pomocą `execl()`. Przekazanie argumentów przez `argv` nie jest tutaj wykorzystywane - używany go w innych częściach systemu (np. przy uruchamianiu lekarzy).

---

### B. Pamięć dzielona i synchronizacja (Struktura StanSOR)

Centralna struktura `StanSOR` w pamięci dzielonej przechowuje globalny stan systemu, dostępny dla wszystkich procesów.

#### B.1. Definicja struktury StanSOR

[definicja StanSor: wspolne.h](https://github.com/utratav/SO_SOR/blob/c636695b4c763e3f9b6acc920a17a64fc873c2d2/wspolne.h#L83-L98)



#### B.2. Inicjalizacja pamięci dzielonej

Pamięć dzielona jest tworzona i inicjalizowana w procesie `main`:

[inicjalizacja shm w main: main.c](https://github.com/utratav/SO_SOR/blob/c636695b4c763e3f9b6acc920a17a64fc873c2d2/main.c#L202-L207)



Wszystkie pola są zerowane przez `memset()`, po czym ustawiane są wartości początkowe: flaga symulacji oraz dostępność wszystkich specjalistów.

#### B.3. Ochrona dostępu mutexem (SEM_DOSTEP_PAMIEC)

Każda* operacja na strukturze `StanSOR` musi być otoczona sekcją krytyczną z wykorzystaniem semafora `SEM_DOSTEP_PAMIEC`:
[przyklad dla pacjenta: pacjent.c](https://github.com/utratav/SO_SOR/blob/c636695b4c763e3f9b6acc920a17a64fc873c2d2/pacjent.c#L44-L73)


Wzorzec `while(semop(...) == -1) { if (errno == EINTR) continue; }` zapewnia poprawną obsługę przerwań sygnałami podczas oczekiwania na semafor.

---

### C. Dynamiczne zarządzanie okienkami rejestracji

System implementuje dynamiczne otwieranie i zamykanie drugiego okienka rejestracji w oparciu o aktualne obciążenie kolejki.

#### C.1. Progi otwarcia i zamknięcia

Progi są zdefiniowane jako makra w `wspolne.h`:

```c
// wspolne.h - definicje progów
#define PROG_OTWARCIA (MAX_PACJENTOW / 2)  
#define PROG_ZAMKNIECIA (MAX_PACJENTOW / 3)
```

Rożne progi zwalniające zapobiegają oscylacji stanu przy granicznym obciążeniu.

#### C.2. Detekcja progu przez proces pacjenta

Proces pacjenta przy wejściu do poczekalni sprawdza długość kolejki i ustawia flagę `wymuszenie_otwarcia` w pamięci dzielonej:

[flaga w shm: pacjent.c](https://github.com/utratav/SO_SOR/blob/c636695b4c763e3f9b6acc920a17a64fc873c2d2/pacjent.c#L57-L68)


Przy 10 000 procesów istnieje 99.99% szans, że to pacjent pierwszy wykryje potrzebę otwarcia drugiej bramki - korzystamy z tego, że akurat wchodzi do pamięci dzielonej chronionej mutexem.
Pełni on rolę czujnika - gdy nadejdzie potrzeba ustawia flage wymuszenia otwarcia.

#### C.3. Wątek bramki w procesie main (watek_bramka)

Właściwe sterowanie okienkiem odbywa się w dedykowanym wątku `watek_bramka` działającym w procesie `main`. Wątek sprawdza flagę `wymuszenie_otwarcia` i wykonuje odpowiednie akcje:

[watek_bramka: main.c](https://github.com/utratav/SO_SOR/blob/c636695b4c763e3f9b6acc920a17a64fc873c2d2/main.c#L77-L129)


Co ważne, wątek nie wchodzi do pamięci dzielonej nie korzystając z mutexu - flaga otwarcia drugiej bramki jest atomowa 0/1 - przez to reakcja jest niemalże natychmiastowa.

Wątek utrzymuje lokalny stan `local_okienko_otwarte`, który jest synchronizowany z pamięcią dzieloną. Dzięki temu inne procesy mogą odczytać aktualny status okienka.

#### C.4. Proces rejestracji

Proces rejestracji (`rejestracja.c`) w pętli odbiera komunikaty od pacjentów i odsyła je z powrotem (z ustawionym `mtype` na PID pacjenta). Zawiera trzy istotne mechanizmy bezpieczeństwa:

1. **Weryfikacja istnienia pacjenta** – przed wysłaniem odpowiedzi, rejestracja sprawdza za pomocą `kill(pid, 0)` czy proces pacjenta nadal żyje. Jeśli pacjent zakończył działanie (np. przez `SEM_UNDO` po sygnale), wiadomość jest anulowana zamiast zapychać kolejkę martwymi odpowiedziami.

2. **Dynamiczny bufor przepełnienia** – wysyłanie odpowiedzi odbywa się z flagą `IPC_NOWAIT`. Gdy kolejka jest pełna (`EAGAIN`), zamiast blokowania, pacjent trafia do dynamicznego bufora w pamięci procesu. Bufor jest alokowany na starcie na podstawie rzeczywistej pojemności kolejki systemowej (`msg_qbytes / sizeof_msg`).

3. **Opróżnianie bufora** – na początku każdej iteracji pętli, jeśli bufor zawiera oczekujące wiadomości, rejestracja sprawdza stan kolejki przez `msgctl(IPC_STAT)` i próbuje odesłać zbuforowane komunikaty dopóki kolejka ma wolne miejsce.

[główna pętla rejestracji: rejestracja.c](https://github.com/utratav/SO_SOR/blob/c636695b4c763e3f9b6acc920a17a64fc873c2d2/rejestracja.c#L63-L121)



Flaga `IPC_NOWAIT` w `msgrcv()` sprawia, że proces nie blokuje się gdy kolejka jest pusta – zamiast tego zwraca błąd `ENOMSG`, co pozwala na responsywne sprawdzanie flagi `koniec_pracy`.

Ujemna wartość `-2` w argumencie `msgtype` oznacza odbiór wiadomości o typie <= 2 z priorytetem dla mniejszych wartości (VIP=1 przed Zwykły=2).

---

### D. System kolejek komunikatów

#### D.1. Struktura komunikatu pacjenta

[struktura dla kolejek: wspolne.h](https://github.com/utratav/SO_SOR/blob/c636695b4c763e3f9b6acc920a17a64fc873c2d2/wspolne.h#L100-L108)


Pole `mtype` pełni podwójną rolę:
* **Przy wysyłaniu DO lekarza**: oznacza priorytet (1=VIP/Czerwony, 2=Żółty/Zwykły, 3=Zielony)
* **Przy odsyłaniu DO pacjenta**: przyjmuje wartość `pacjent_pid`, umożliwiając precyzyjne adresowanie

#### D.2. Tworzenie kolejek komunikatów

Wszystkie kolejki są tworzone w procesie `main` za pomocą funkcji pomocniczej:

[msg_creat: main.c](https://github.com/utratav/SO_SOR/blob/c636695b4c763e3f9b6acc920a17a64fc873c2d2/main.c#L146-L150)

#### D.3. Przepływ komunikatów i cykl priorytetów

Komunikat pacjenta przechodzi przez system w następujący sposób:

```
PACJENT -> REJESTRACJA -> PACJENT -> POZ -> PACJENT -> SPECJALISTA -> PACJENT
```

Na każdym etapie `mtype` jest odpowiednio modyfikowany. Dorosły pacjent wykonuje operacje IPC samodzielnie za pomocą funkcji `wykonaj_ipc_samodzielnie()`, natomiast dziecko deleguje je do wątku opiekuna (szczegóły w późniejszej sekcji):

[funkcja wykonaj_ipc_samodzielnie: pacjent.c](https://github.com/utratav/SO_SOR/blob/c636695b4c763e3f9b6acc920a17a64fc873c2d2/pacjent.c#L84-L90)



Funkcja ta opakowuje pełen cykl komunikacji: zajęcie semafora limitującego, wysłanie wiadomości, oczekiwanie na odpowiedź adresowaną po PID, zwolnienie semafora. 

[przyklad wykonania: pacjent.c](https://github.com/utratav/SO_SOR/blob/c636695b4c763e3f9b6acc920a17a64fc873c2d2/pacjent.c#L208-L225)


#### D.4. Odbiór z priorytetem (msgrcv z ujemnym mtype)

Lekarze specjaliści odbierają wiadomości z priorytetem dla pilniejszych przypadków:

[przykład dla lekarza specjalisty: lekarz.c](https://github.com/utratav/SO_SOR/blob/c636695b4c763e3f9b6acc920a17a64fc873c2d2/lekarz.c#L186-L189)



Wartość `-3` oznacza: "odbierz wiadomość o typie <= 3, wybierając najpierw tę z najmniejszym typem". Skutkuje to obsługą w kolejności: Czerwony (1) → Żółty (2) → Zielony (3).

#### D.5. Limitowanie kolejek (semafory SLIMIT)

Aby zapobiec przepełnieniu systemowych buforów kolejek komunikatów, wprowadzono mechanizm ograniczania producenta:
[2 funkcje dla pacjenta: pacjent.c](https://github.com/utratav/SO_SOR/blob/c636695b4c763e3f9b6acc920a17a64fc873c2d2/pacjent.c#L75-L82)



Semafor jest zwalniany dopiero po odebraniu odpowiedzi od lekarza, co gwarantuje, że w każdej kolejce nigdy nie będzie więcej niż `INT_LIMIT_KOLEJEK`  oczekujących komunikatów.

---

### E. Logika lekarzy (lekarz.c)

#### E.1. Rozróżnienie typów lekarzy

Typ lekarza jest przekazywany jako argument przy uruchomieniu procesu:

[uruchamianie lekarzy](https://github.com/utratav/SO_SOR/blob/c636695b4c763e3f9b6acc920a17a64fc873c2d2/main.c#L222-L227)



#### E.2. Praca lekarza POZ (Triaż)

Lekarz POZ przypisuje pacjentowi kolor triażu i kieruje do odpowiedniego specjalisty. Analogicznie do rejestracji, POZ posiada mechanizm weryfikacji istnienia pacjenta oraz dynamiczny bufor przepełnienia:

[pętla główna lekarz POZ: lekarz.c](https://github.com/utratav/SO_SOR/blob/c636695b4c763e3f9b6acc920a17a64fc873c2d2/lekarz.c#L54-L119)


**Własna interpretacja:** Przy odesłaniu pacjenta przez POZ do domu nadajemy kolor 0 - niezdefiniowany. Uznaję tym samym, że pacjent kończy tutaj swoje badania i zwyczajnie
w jego przypadku priorytet jest nieistotny (zdrowy).


#### E.3. Praca lekarza specjalisty

Specjalista podejmuje końcową decyzję o dalszym postępowaniu. Identycznie jak POZ i rejestracja, specjalista posiada mechanizm weryfikacji istnienia pacjenta (`kill(pid, 0)`) oraz dynamiczny bufor przepełnienia:

[pętla główna lekarza specjalisty: lekarz.c](https://github.com/utratav/SO_SOR/blob/c636695b4c763e3f9b6acc920a17a64fc873c2d2/lekarz.c#L146-L216)



**Podsumowanie mechanizmów bezpieczeństwa w procesach SOR (rejestracja, POZ, specjaliści):**

Wszystkie trzy typy procesów obsługujących pacjentów implementują identyczny wzorzec ochronny:

1. **`kill(pacjent_pid, 0)` przed `msgsnd()`** – sprawdzenie czy proces pacjenta istnieje. Eliminuje wysyłanie odpowiedzi do martwych procesów, co zapobiegałoby zapychaniu kolejki wiadomościami, których nikt nie odbierze.

2. **`IPC_NOWAIT` na `msgsnd()`** – nieblokujące wysyłanie. Gdy kolejka jest pełna, proces nie zawiesza się lecz otrzymuje `EAGAIN`, co pozwala na dalsze działanie.

3. **Dynamiczny bufor (`malloc` na starcie)** – przy `EAGAIN` komunikat trafia do bufora w pamięci procesu. Na początku każdej iteracji pętli bufor jest opróżniany do kolejki gdy ta ma wolne miejsce (weryfikowane przez `msgctl(IPC_STAT)`).

4. **`pobierz_stan_kolejki()` (`msgctl IPC_STAT`)** – odczytanie `msg_qnum` (aktualnej liczby wiadomości) pozwala podejmować inteligentne decyzje o próbach odesłania zbuforowanych komunikatów.

---

### F. Obsługa sygnałów: Wezwanie lekarza na oddział (SIGUSR2)

#### F.1. Konfiguracja handlera w procesie lekarza
[handler: lekarz.c](https://github.com/utratav/SO_SOR/blob/c636695b4c763e3f9b6acc920a17a64fc873c2d2/lekarz.c#L9-L16)


Handler jedynie ustawia flagę – właściwa logika wykonuje się w pętli głównej.
Każda nazwa procesu specjalisty rozpoczyna się od `SOR_S_*Nazwa_specjalisty*` - jest to pomocne przy egzekwowaniu sygnału z konsoli.
[handler dla lekarz.c](https://github.com/utratav/SO_SOR/blob/c8652d99de38b705092039fcc68566cfc2ebc569/lekarz.c#L7-L14)

#### F.2. Logika opuszczenia SOR przez lekarza

[SIGUSR2 - wezwanie na oddzial: lekarz.c](https://github.com/utratav/SO_SOR/blob/c636695b4c763e3f9b6acc920a17a64fc873c2d2/lekarz.c#L148-L167)



Możemy na bieżąco monitorować dyżurujących specjalistów w  *spec_na_oddziale.txt*

[logika wezwanie na oddział](https://github.com/utratav/SO_SOR/blob/c636695b4c763e3f9b6acc920a17a64fc873c2d2/lekarz.c#L148-L167)

#### F.3. Proces Dyrektora (opcjonalny)

Dyrektor jest uruchamiany gdy program `main` otrzyma argument `auto`:

[PROCES dyrektora: main.c](https://github.com/utratav/SO_SOR/blob/c636695b4c763e3f9b6acc920a17a64fc873c2d2/main.c#L236-L253)

**Przy uruchamianiu wpisz w konsoli:** *./main auto*

Dyrektor ignoruje sygnały zakończenia, ponieważ sam musi być aktywnie zabity przez `main` podczas procedury ewakuacji (`SIGKILL`).



---

### G. Obsługa sygnałów: Procedura ewakuacji (SIGINT)

#### G.1. Handler w procesie main

[handler w main]()

```c
// main.c - handler i flaga
volatile sig_atomic_t ewakuacja_rozpoczeta = 0;

void signal_handler(int sig) {
    if (sig == SIGINT) ewakuacja_rozpoczeta = 1;
}

// Konfiguracja w main():
struct sigaction sa;
sa.sa_handler = signal_handler;
sigemptyset(&sa.sa_mask);
sa.sa_flags = 0;
sigaction(SIGINT, &sa, NULL);
signal(SIGTERM, SIG_IGN);  // Main ignoruje SIGTERM
```

#### G.2. Funkcja przeprowadz_ewakuacje()

[przeprowadz ewakuacje](https://github.com/utratav/SO_SOR/blob/c636695b4c763e3f9b6acc920a17a64fc873c2d2/main.c#L164-L176)



#### G.3. Procedura ewakuacji w generatorze

Generator po otrzymaniu SIGINT wykonuje procedurę:

[ewakuacja generator](https://github.com/utratav/SO_SOR/blob/c636695b4c763e3f9b6acc920a17a64fc873c2d2/generuj.c#L24-L63)



Snapshot umożliwia późniejszą weryfikację, czy liczba ewakuowanych pacjentów zgadza się z liczbą obecnych w systemie.

#### G.4. Handler ewakuacji w procesie pacjenta

[pacjent.c](https://github.com/utratav/SO_SOR/blob/c636695b4c763e3f9b6acc920a17a64fc873c2d2/pacjent.c#L151-L156)


Pacjent używa `_exit()` zamiast `exit()` aby uniknąć wykonywania procedur sprzątających, które mogłyby powodować problemy podczas ewakuacji.

Argumentem _exit() jest wartość sem_op_miejsca - jest to ta sama zmienna która kontroluje o ile wykonujemy V/P na semaforze (rodzic z dzieckiem = 2, dorosły = 1).

---

### H. Wątki monitorujące i raportujące

#### H.1. Wątek statystyk (watek_statystyki)

Zbiera statystyki z dedykowanej kolejki komunikatów:

[watek statystyki: main.c](https://github.com/utratav/SO_SOR/blob/c636695b4c763e3f9b6acc920a17a64fc873c2d2/main.c#L47-L74)


Struktura `StatystykiLokalne` jest chroniona mutexem `pthread_mutex_t stat_mutex`, ponieważ może być odczytywana z głównego wątku podczas generowania raportu końcowego.

#### H.2. Wątek raportu specjalistów (watek_raport_specjalistow)

Okresowo zapisuje status dostępności specjalistów do pliku:

[watek dla specjalistow: main.c](https://github.com/utratav/SO_SOR/blob/c636695b4c763e3f9b6acc920a17a64fc873c2d2/main.c#L30-L44)



Plik `spec_na_oddziale.txt` jest nadpisywany przy każdej iteracji (`"w"` zamiast `"a"`), co zapewnia aktualny stan.

#### H.3. Zarządzanie cyklem życia wątków

Wszystkie wątki są tworzone na początku i kończone na końcu symulacji:

```c
// main.c - tworzenie wątków
pthread_create(&stat_tid, NULL, watek_statystyki, NULL);
pthread_create(&bramka_tid, NULL, watek_bramka, NULL);
pthread_create(&raport2_tid, NULL, watek_raport_specjalistow, NULL);

// main.c - kończenie wątków
if (!ewakuacja_rozpoczeta) {
    monitor_running = 0;  // Sygnalizacja zakończenia
    // ...
}

pthread_join(stat_tid, NULL);
pthread_join(bramka_tid, NULL);
pthread_join(raport2_tid, NULL);
```

Flaga `volatile int monitor_running` kontroluje pętle główne wszystkich wątków. Po jej wyzerowaniu, wątki kończą bieżącą iterację i wychodzą z funkcji.

---

### I. Wątek opiekuna w procesie pacjenta (model zleceniowy)

Zgodnie z wymaganiami projektu, dzieci poniżej 18 lat przychodzą na SOR pod opieką osoby dorosłej. Opiekun jest realizowany jako wątek POSIX w procesie pacjenta. Zamiast pasywnego oczekiwania, opiekun działa w modelu zleceniowym – wątek główny (dziecko) deleguje opiekunowi konkretne zadania IPC, a opiekun je wykonuje i raportuje zakończenie.

#### I.1. Blok sterujący opiekuna (OpiekunControlBlock)

Synchronizacja między wątkiem dziecka a wątkiem opiekuna opiera się na dedykowanej strukturze z mutexem i zmiennymi warunkowymi:

[synchronizacja wątków: pacjent.c](https://github.com/utratav/SO_SOR/blob/c636695b4c763e3f9b6acc920a17a64fc873c2d2/pacjent.c#L21-L39)


Struktura `OpiekunControlBlock` realizuje wzorzec producent-konsument: dziecko (producent) ustawia zadanie i sygnalizuje `cond_start`, opiekun (konsument) wykonuje zadanie i sygnalizuje `cond_koniec`.

#### I.2. Funkcja zlecająca zadanie opiekunowi

[pacjent.c](https://github.com/utratav/SO_SOR/blob/c636695b4c763e3f9b6acc920a17a64fc873c2d2/pacjent.c#L92-L100)


Funkcja jest blokująca – dziecko czeka (`cond_wait`) dopóki opiekun nie ustawi `aktualne_zadanie` z powrotem na `BRAK_ZADAN`, co oznacza wykonanie zlecenia.

#### I.3. Funkcja wątku opiekuna

[watek opiekun: pacjent.c](https://github.com/utratav/SO_SOR/blob/c636695b4c763e3f9b6acc920a17a64fc873c2d2/pacjent.c#L102-L149)

Opiekun wykorzystuje tę samą funkcję `wykonaj_ipc_samodzielnie()` co dorosły pacjent, dzięki czemu logika komunikacji IPC jest współdzielona. Kluczowa różnica polega na tym, że opiekun operuje na wskaźniku do `OpiekunSync.dane_pacjenta` – współdzielonego bufora komunikatu, przez który dziecko i opiekun wymieniają się danymi.

Wątek opiekuna wykonuje operacje na semaforze z flagą `SEM_UNDO` – oznacza to, że przy awaryjnym zakończeniu procesu, zarówno miejsce dziecka jak i opiekuna zostaną zwolnione niezależnie.

#### I.4. Tworzenie wątku opiekuna i zajmowanie podwójnego miejsca

[inicjalizacja watku: pacjent.c](https://github.com/utratav/SO_SOR/blob/c636695b4c763e3f9b6acc920a17a64fc873c2d2/pacjent.c#L190-L203)


Zmienna `sem_op_miejsca` przyjmuje wartość 1 dla dorosłych lub 2 dla nieletnich z opiekunem.

#### I.5. Przebieg wizyty dziecka – delegacja do opiekuna

Dziecko (wątek główny) zajmuje jedno miejsce w poczekalni bezpośrednio, a drugie miejsce zleca opiekunowi. Następnie każdy etap komunikacji IPC (rejestracja, POZ, specjalista) jest delegowany do opiekuna przez `zlec_opiekunowi()`:

[przykład z kodu: pacjent.c](https://github.com/utratav/SO_SOR/blob/c636695b4c763e3f9b6acc920a17a64fc873c2d2/pacjent.c#L208-L256)


Zadanie `ZADANIE_KONIEC` powoduje wyjście opiekuna z pętli głównej. `pthread_join()` czeka na faktyczne zakończenie wątku i zwalnia jego zasoby.

---

### J. System stanów pacjenta

#### J.1. Definicje stanów

```c
// wspolne.h - stany pacjenta
#define STAN_PRZED_SOR 0      // Czeka na wejście
#define STAN_W_POCZEKALNI 1   // Wewnątrz SOR
#define STAN_WYCHODZI 2       // Zakończona obsługa
```


### K. Funkcje pomocnicze

#### K.1. Funkcja zapisz_raport()

[zapisz_raport: wspolne.h](https://github.com/utratav/SO_SOR/blob/c636695b4c763e3f9b6acc920a17a64fc873c2d2/wspolne.h#L134-L148)



Funkcja używa `write()` zamiast `printf()` / `fprintf()`, co jest bezpieczniejsze w kontekście signal-handlerów. Pierwszym argumentem funkcji jest wyjście - plik raportowy, bądź konsola, semid - pozostałość po starej implementacji.

#### K.2. Funkcja podsumowanie()

Generuje raport końcowy porównujący obserwowane statystyki z wartościami oczekiwanymi:

[funkcja ostatecznego podsumowania: wspolne.h](https://github.com/utratav/SO_SOR/blob/c636695b4c763e3f9b6acc920a17a64fc873c2d2/wspolne.h#L150-L194)



#### K.3. Funkcja uruchom_proces()

Wrapper standaryzujący tworzenie procesów potomnych:

[tworzenie procesów potomnych - funkcja pomocnicza: main.c](https://github.com/utratav/SO_SOR/blob/c636695b4c763e3f9b6acc920a17a64fc873c2d2/main.c#L152-L162)



Przywrócenie domyślnej obsługi sygnałów w procesie potomnym jest kluczowe, ponieważ dyspozycje sygnałów są dziedziczone po `fork()`.

#### K.4. Funkcja czyszczenie()


Zwalnia wszystkie zasoby IPC:
[czyszczenie: main.c](https://github.com/utratav/SO_SOR/blob/c636695b4c763e3f9b6acc920a17a64fc873c2d2/main.c#L133-L140)


Flaga `IPC_RMID` oznacza natychmiastowe usunięcie zasobu. Dla pamięci dzielonej, faktyczne zwolnienie nastąpi gdy ostatni proces odłączy się od segmentu.

---

## Podręcznik do semaforów

| Zbiór (klucz) | Indeks | Nazwa | Wartość Początkowa | Opis działania |
| :--- | :--- | :--- | :--- | :--- |
| **ID_SEM_SET ('M')** | 0 | **SEM_DOSTEP_PAMIEC** | **1** | Mutex chroniący strukturę StanSOR przed jednoczesnym zapisem (race conditions). |
| **ID_SEM_SET ('M')** | 1 | **SEM_MIEJSCA_SOR** | **MAX_PACJENTOW** | Licznik miejsc w poczekalni. Blokuje wejście przy pełnym obłożeniu. |
| **ID_SEM_SET ('M')** | 2 | **SEM_ZAPIS_PLIK** | **1** | Mutex I/O (obecnie nieużywany - zachowany dla kompatybilności). |
| **ID_SEM_SET ('M')** | 3 | **SEM_GENERATOR** | **MAX_PROCESOW** | Limit jednoczesnych procesów pacjentów. Chroni przed fork-bombą. |
| **ID_SEM_LIMITS ('X')** | 0 | **SLIMIT_REJESTRACJA** | **INT_LIMIT_KOLEJEK** | Limit komunikatów w kolejce rejestracji. |
| **ID_SEM_LIMITS ('X')** | 1 | **SLIMIT_POZ** | **INT_LIMIT_KOLEJEK** | Limit komunikatów w kolejce POZ. |
| **ID_SEM_LIMITS ('X')** | 2-7 | **SLIMIT_[SPECJALISTA]** | **INT_LIMIT_KOLEJEK** | Limity dla kolejek specjalistów (Kardiolog, Neurolog, Laryngolog, Chirurg, Okulista, Pediatra). |

---

## Podręcznik do kolejek komunikatów

| Klucz | Identyfikator | Przeznaczenie | Typy wiadomości |
| :--- | :--- | :--- | :--- |
| **'R'** | ID_KOLEJKA_REJESTRACJA | Komunikacja pacjent ↔ rejestracja | mtype=1 (VIP), mtype=2 (Zwykły), mtype=PID (odpowiedź) |
| **'P'** | ID_KOLEJKA_POZ | Komunikacja pacjent ↔ lekarz POZ | mtype=1 (do POZ), mtype=PID (odpowiedź) |
| **'K'** | ID_KOL_KARDIOLOG | Komunikacja pacjent ↔ kardiolog | mtype=1,2,3 (kolor), mtype=PID (odpowiedź) |
| **'N'** | ID_KOL_NEUROLOG | Komunikacja pacjent ↔ neurolog | j.w. |
| **'L'** | ID_KOL_LARYNGOLOG | Komunikacja pacjent ↔ laryngolog | j.w. |
| **'C'** | ID_KOL_CHIRURG | Komunikacja pacjent ↔ chirurg | j.w. |
| **'O'** | ID_KOL_OKULISTA | Komunikacja pacjent ↔ okulista | j.w. |
| **'D'** | ID_KOL_PEDIATRA | Komunikacja pacjent ↔ pediatra | j.w. |
| **'T'** | ID_KOLEJKA_STATYSTYKI | Zbieranie statystyk przez wątek | mtype=1 (StatystykaPacjenta) |

---

## Stałe konfiguracyjne

| Stała | Wartość / maksymalna przepustowość | Opis |
| :--- | :--- | :--- |
| **PACJENCI_NA_DOBE** | max zakres int | Całkowita liczba pacjentów do wygenerowania |
| **MAX_PACJENTOW** | < 32767 | Maksymalna pojemność poczekalni SOR |
| **MAX_PROCESOW** | < 32767 | Maksymalna liczba jednoczesnych procesów pacjentów |
| **INT_LIMIT_KOLEJEK** | < 628 | Maksymalna liczba komunikatów w każdej kolejce |
| **PROG_OTWARCIA** | MAX_PACJENTOW/2 | Próg otwarcia drugiego okienka |
| **PROG_ZAMKNIECIA** | MAX_PACJENTOW/3 | Próg zamknięcia drugiego okienka |


---



  ## Kompilacja i uruchomienie

### Wymagania

* **Kompilator:** gcc
* **System operacyjny:** Linux 
* **Biblioteki:** pthread, standardowe biblioteki systemowe IPC

### Kompilacja

Projekt wykorzystuje `Makefile` do kompilacji. Wszystkie pliki źródłowe są kompilowane z flagami `-Wall` (wszystkie ostrzeżenia) oraz `-pthread` (dla wątków).

```bash
# Kompilacja wszystkich programów
make all

# Lub po prostu:
make
```

Makefile tworzy następujące pliki wykonywalne:
* `main` – główny proces zarządzający symulacją
* `pacjent` – program procesu pacjenta
* `lekarz` – program procesu lekarza (POZ i specjaliści)
* `rejestracja` – program procesu rejestracji
* `generator` – generator procesów pacjentów

### Uruchomienie

#### Tryb podstawowy (bez automatycznego dyrektora)

```bash
./main
```

W tym trybie symulacja działa bez automatycznych wezwań lekarzy na oddział. Wezwania można wysyłać ręcznie z konsoli za pomocą polecenia `kill`:

```bash
# Wysłanie sygnału SIGUSR2 do konkretnego lekarza (wymaga znajomości PID)
kill -SIGUSR2 
```
**Ewentualnie:**
```bash
# Nie wymaga znajmości PID
pkill -SIGUSR2 -f SOR_S_*Nazwa_specjalisty*
```

#### Tryb automatyczny (z procesem dyrektora)

```bash
./main auto
```

W tym trybie uruchamiany jest dodatkowy proces dyrektora, który losowo wybiera specjalistów i wysyła im sygnały wezwania na oddział (`SIGUSR2`). Interwał między wezwaniami wynosi 2-6 sekund.

### Zatrzymanie symulacji

#### Normalne zakończenie

Symulacja kończy się automatycznie po wygenerowaniu i obsłużeniu wszystkich pacjentów (`PACJENCI_NA_DOBE`).

#### Ewakuacja (przerwanie)

```bash
# Naciśnij Ctrl+C w terminalu z uruchomionym ./main
# lub wyślij sygnał SIGINT:
kill -SIGINT 
```

Wywołuje to procedurę ewakuacji – wszystkie procesy są bezpiecznie zamykane, a na końcu wyświetlany jest raport z danymi o ewakuowanych pacjentach.

### Czyszczenie

```bash
# Usunięcie plików wykonywalnych i raportów
make clean

# Usunięcie zasobów IPC (w przypadku niespodziewanego zakończenia)
make ipc_clean
```

---

# Testy

## T1: Czy Użycie semafora przed kolejką komunikatów skutecznie uniemożliwia zapchanie kolejki i w konsekwencji chroni przed deadlockiem (exit pacjenta w sekcji krytycznej)?

Będe używał stwierdzenia procesy SOR - chodzi mi o rejestracje, POZ, specjalistów - implementacja komunikacji pomiędzy pacjentem, a każdym procesem SOR jest taka sama.

**Założena:**

* Każda z 8 kolejek komunikatów posiada osobisty semafor obniżany w przed wysłaniem wiadomości do procesu SOR, podwyższany po otrzymaniu wiadomości zwrotnej.
  
* Każda kolejka ma maksymalnie 682 slotów (systemowy limit w bajtach / sizeof(msg) - sizeof(long)) - pozostałe procesy czekają. Wartość semaforów musi być < 682.
  
* Każdy proces SOR przyjmuję według ustalonego wcześniej priorytetu - Pacjenci VIP / Pacjenci z kolorem czerwonym powinni być obsłużeni szybciej od pozostałych.

**Potencjalne problemy:**

* Każdy Pacjent wykonuje operację semop z flagą `SEM_UNDO` - jest ona newralgiczna z bardzo prostego powodu - kiedy Pacjent zexituje / dostanie sygnał terminujący w sekcji krytycznej (przed podwyższeniem semafora, który wcześniej obniżał)
  System automatycznie wykona inkrementację na semaforze o wartości op, którą proces użył "wchodząc" przez semafor - nie blokując tym samym semafora do końca wykonywania programu.
  
* Użycie `SEM_UNDO` jest wskazane, ale budzi ono szereg innych problemów. Jeśli semafor jest jedyną blokadą przed faktyczną drogą komunikacji istnieje możliwość, że pacjent po wysłaniu wiadomości do processu SOR, a jeszcze przed
  otrzymaniem wiadomości zwrotnej dostanie sygnał terminujący jego działanie (w testach zwykły exit wystarczy). System automatycznie dokona inkrementacji na semaforze i w konsekwencji przez semafor wejdzie kolejny pacjent.
  Oczywiście jeden exitujacy proces nie spowoduje wiele szkód, ale co jeśli takich pacjentów jest 500? Nagle Kolejka jest 1000 wiadomości, a proces SOR - np. rejestracja nie ma wolnych slotów do wysłania wiadomości zwrotnej - mamy deadlocka.
  
* Od razu nasuwa się pomysł żeby po msgrcv proces SOR patzrył czy proces w ogóle istnieje - znamy jego PID z wiadomośći, którą przesyła oraz posiadamy permisję - wystarczy kill(pid, 0). Jednak to tylko częściowo rozwiąże nasz problem z prostego powodu:

**Tak wygląda nasza kolejka gdy pierwsze 500 pacjentów obniża semafor i wysyła wiadomośc i załóżmy, że robi sleep(5) - nie robi msgrcv:**

<img width="956" height="103" alt="Zrzut ekranu 2026-02-06 183712" src="https://github.com/user-attachments/assets/4a153568-85e3-42c2-ad20-d0338a2766b7" />


Mamy FIFO posortowane według priorytetu, gdzie vip o wartość | -1 | będzie obsłużony szybciej od zwykłego | -2 |



W przypadku wykonania `SEM_UNDO` przez semafor wchodzi kolejne 500 osób, ich wiadomości są wymieszane z tymi, którzy już nie żyją, ALE co najważniejsze - posortowane według priorytetu.

<img width="995" height="77" alt="Zrzut ekranu 2026-02-06 183721" src="https://github.com/user-attachments/assets/ed80e1bc-efd1-4030-8c43-3b918317b622" />

szary kolor oznacza nieżywych pacjentów*

Proces SOR zobaczy, że pierwsze 3 pacjentów już nie istnieje - skutecznie opróżni kolejke o te (3) wiadomości, jednak następnie trafi na "żywego" vip i się zablokuje bo l. wiadomości nadal jest > 682 (limit).

* Rozwiązaniem jest dynamiczny bufor, który - gdy kolejka jest pełna - zapisze strukture żywego pacjenta, następnie przejdzie do anulowania kolejnego nieżywego pacjenta.


**Przebieg testu:**

* rejestracja wykonuje sleep(60) - czeka na zapchanie kolejki

* wchodzi pierwsze 500 pacjentow - obnizaja semafor, robia exit(0) pomiedzy msgsnd, a msgrcv.
  
* <img width="561" height="174" alt="Zrzut ekranu 2026-02-05 040232" src="https://github.com/user-attachments/assets/2ff81e93-b47d-4a08-8cc8-2c90deb0afce" />

  


* w ipcs -q nadal widzimy 500 wiadomości - wiadomości istnieją - pacjenci nie.

* wchodzi kolejne 500 pacjentów - zapełniają kolejkę do maksymalnego limitu.

* rejestracja się budzi i dokonuje czyszczenia
  
* <img width="648" height="548" alt="Zrzut ekranu 2026-02-05 040407" src="https://github.com/user-attachments/assets/9ff3e69e-9d1f-4d02-930e-8056198f86ba" />




**Podgląd w IPCS:**
* <img width="466" height="162" alt="Zrzut ekranu 2026-02-05 040438" src="https://github.com/user-attachments/assets/a5eebcf2-1a58-4cc8-9765-0e472a7e9b7a" />






## T2: Użycie sygnału SIG_LEKARZ_ODDZIAŁ na wszystkich specjalistach równocześnie

**Modyfikacje:**
* Przed rozpoczęciem pętli głownej - każdy specjalisty robi:
```c
raise(SIG_LEKARZ_ODDZIAL);

    while(!koniec_pracy)
    {
        ...
    }
```

* analogicznym byłoby użycie komendy: pkill -SIGUSR2 -f SOR_S_
  
* na potrzeby testu lekarz specjalista śpi dokładnie 10 sekund
  

**Przebieg testu:**

* Stan przed:
* <img width="218" height="164" alt="Zrzut ekranu 2026-02-05 201251" src="https://github.com/user-attachments/assets/34cf8e6d-87f8-42fb-83cf-3b96747eaf52" />

* Stan w trakcie sleepa lekarzy:
* <img width="472" height="563" alt="Zrzut ekranu 2026-02-05 201322" src="https://github.com/user-attachments/assets/2a85a6a9-78ca-423f-9de2-fb2cc2f23a28" />

* W tym momencie ostatnia kolejka: pacjent -> main jest mocno przeciążona - w pewnym momencie każdy pacjent już skończył swój cykl i nie będziemy dostawać logów na konsoli - jest to OK.
* <img width="904" height="341" alt="Zrzut ekranu 2026-02-05 201601" src="https://github.com/user-attachments/assets/d18d5626-032d-4d65-b5fe-2eac9fc09070" />

* Po ok. minucie program kończy się sukcesem - kolejki zostały usunięte
* <img width="942" height="572" alt="Zrzut ekranu 2026-02-05 201738" src="https://github.com/user-attachments/assets/237ee688-d66d-41e0-95cd-5542a6121d40" />




## T3: (Poprawiony test) SIGTSTP, SIGCONT i działanie kolejki


**Przebieg:**

* W lekarz POZ robimy sleep(5); - chcemy zebrac jak najwięcej wiadomości od pacjentów
  
* wykonujemy CTRL + Z (SIGTSTP) do wszystkich procesów
  
* <img width="887" height="578" alt="Zrzut ekranu 2026-02-05 223708" src="https://github.com/user-attachments/assets/b4f55c2b-d0bc-4fee-a763-771f5dcbeb3a" />

* wykonujemy SIGCONT na samym lekarzu POZ
  
* POZ się budzi i i wykona 500 razy pętle msgrcv msgsnd po czym się zatrzyma
  
* <img width="884" height="577" alt="Zrzut ekranu 2026-02-05 223755" src="https://github.com/user-attachments/assets/70e649e1-7f96-4322-a842-2aa9fd351137" />

* Liczba wiadomośći nadal wynosi 500 - ale są to wiadomości do odebrania przez pacjentów
  
* Po wypisaniu 500 logów konsola stoi - dopóki nie wznowimy wszystkich procesów przez SIGCONT


**Co poszło nie tak w poprzednim teście?**

**Poprzednia implementacja:**
```c
        if(msgrcv(msgid_poz, &pacjent, rozmiar_msg, 0, IPC_NOWAIT) == -1) 
        {
            if (errno == ENOMSG || errno == EINTR) { usleep(50000); continue; } 
            break;
        }
```

**Obecna:**
```c
        if(msgrcv(msgid_poz, &pacjent, rozmiar_msg, -1, IPC_NOWAIT) == -1) 
        {
            if (errno == ENOMSG || errno == EINTR) { usleep(50000); continue; } 
            break;
        }
```

Jak widać zmieniliśmy jedynie argument msgtyp (4) - W projekcie założyliśmy, że lekarz POZ przyjmuje bez priorytetów, potraktowałem to jako "Bierz pierwszą lepszą wiadomość"
msgtyp = 0 zakłada właśnie taki scenariusz - jednak w przypadku kiedy POZ nie dostaje nowych wiadomości, a stare nie są odbierane przez pacjentów - zaczyna przetwarzać 
te same wiadomości, które przed chwilą sam wysłał. Skutkiem czego jest niekończoncząca się pętla tych samych 500 pacjentów. Lekcja z tego jest następująca: nawet jeśli nie zależy
nam na konkretnym priotytecie - wiadomość musi mieć jakis stały mtype dla wszystkich pacjentów oraz po stronie msgrcv musimy ustawić jakikolwiek msgtyp z tego przedziału żeby odróżnić ją od wiadomości zwrotnej
z mtype = pid_pacjenta. 
Np. "-1" - msgrcv weźmie najmniejszą liczbę mniejszą niż | -1 |.



## T4: Czy pacjenci VIP, z kolerem Czerownym zostaną obsłużeni szybciej od reszty?

typ vip = 1, typ zwykly = 0;      typ czerwony = 1, typ zółty = 2, typ zielony = 3;

**Modyfikacje:**

* Po każdej etapie komunikacji pacjent robi sleep(3);
* logujemy w pliku priorytety.txt

**Wyniki:**
<img width="374" height="885" alt="Zrzut ekranu 2026-02-05 230512" src="https://github.com/user-attachments/assets/8d420d39-2cf7-4aa1-9e89-57f8d950fc5c" />

Zgodnie z założeniami Pacjenci o niższej wartości mtype (wyższym priorytecie) zostali obsłużeni szybciej





## T5: Dynamiczna Bramka nr 2

* **Konfiguracja testowa:** MAX_PACJENTOW = 800, PACJENCI_NA_DOBE = 50000
* **Próg otwarcia:** >= 400 osób
* **Próg zamknięcia:** < 266 osób

Analiza pliku `monitor_bramek.txt` pokazuje poprawne działanie mechanizmu monitorującego otwarcia bramek:

<img width="472" height="497" alt="image" src="https://github.com/user-attachments/assets/3f58fd24-33b2-4559-921d-035a26059240" />

System utrzymuje drugie okienko otwarte mimo spadku poniżej progu otwarcia 400, zamykając je dopiero po osiągnięciu dolnego progu 266.






## T6: Procedura nagłej ewakuacji (SIGINT)

**Cel:** Weryfikacja poprawności mechanizmu "Snapshota" (zamrożenia stanu pamięci) oraz zgodności liczby pacjentów przy nagłym przerwaniu symulacji (Ctrl+C).

**Wyniki:**

<img width="466" height="752" alt="Zrzut ekranu 2026-02-02 083444" src="https://github.com/user-attachments/assets/b6cbb1d9-54e2-49ce-88a8-dd53d3122cd0" />

* Stan w momencie przerwania: Generator zablokował pamięć i wykonał zrzut stanu: 800 pacjentów wewnątrz SOR oraz 118 w kolejce (łącznie 918 procesów).

* Proces usuwania: Wysłano sygnał SIGTERM. Suma kodów wyjścia procesów (waitpid) wyniosła 800, co idealnie pokrywa się z liczbą pacjentów zajmujących zasoby (800 wew.). Pozostałe 118 procesów (kolejka) zwróciło 0.

* Raport: Sekcja "RAPORT EWAKUACJI" wyświetliła poprawne dane (800/118), zgodne ze stanem faktycznym.

* Wniosek: Mechanizm działa prawidłowo. Wyeliminowano ryzyko wyścigu (race condition), a każdy proces został poprawnie zidentyfikowany i rozliczony.

* Dygresja: teoretycznie moglibyśmy zwracać wartość inną niż zero dla osob przed poczekalnią jednak musielibyśmy zadbać o to, żeby generowanie wieku pacjenta i zapis do pamięci dzielonej o pobycie przed poczekalnia odbywały się jak najszybciej.
  Przyznanie wieku mogłoby się odbywać z poziomu fork() i exec() w generatorze - przekazywalibyśmy wiek jako argument (przy odpowiedniej konwersji na stringa) oraz przypisywali atoi(argv[1]) do wieku. Jednak co z pacjentami, którzy nie uaktualnili StanSOR - przed_poczekalnia++.
  Użycie semctl z GETNCNT również nie rozwiąże problemu, gdyż ten traktuje rodzica z dzieckiem jako pojedynczy proces. To samo się tyczy logiki semctl i GETVAL na semaforze generatora w połączeniu z GETVAL semafora poczekalni - znowu różnica zwróci nam jedynie liczbę procesów
  (bez rozróżnienia na dorosły / dziecko z opiekunem)
