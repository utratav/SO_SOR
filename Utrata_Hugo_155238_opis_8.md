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


### A. Tworzenie procesów (Generator Pacjentów)

* Generator działa w pętli for, utworzenie procesu jest poprzedzone wejściem do semafora ograniczającego ilość bieżących procesów.
  [Obniżenie semafora i forkowanie: generuj.c](https://github.com/utratav/SO_SOR/blob/981ff243d3635545750d90b8f0fa4d9046ef8aea/generuj.c#L44-L70)
*  Kiedy pacjent umiera (kończy działanie w pacjent.c przez exit(0)), system wysyła SIGCHLD do generatora.                  
   `while (waitpid(-1, NULL, WNOHANG) > 0)`:  Funkcja odbierająca status zakończenia dziecka.-1: Czekamy na dowolne dziecko.
   Jeśli funkcja zwróci PID skończonego / zabitego procesu - podnosimy semafor
   [Obsługa SIGCHLD: generuj.c](https://github.com/utratav/SO_SOR/blob/981ff243d3635545750d90b8f0fa4d9046ef8aea/generuj.c#L8-L23)
*  Kluczowe flagi struktury sigaction. [Szczegóły: generuj.c](https://github.com/utratav/SO_SOR/blob/981ff243d3635545750d90b8f0fa4d9046ef8aea/generuj.c#L27-L31)
  
   `SA_RESTART`: Po obsłużeniu sygnału SIGCHLD automatycznie wznowi przerwaną funkcję systemową (semop ) inaczej  zwróciłaby błąd -1 z errno = EINTR.
   
   `SA_NOCLDSTOP`: System wyśle sygnał SIGCHLD tylko wtedy, gdy dziecko zakończy działanie (exit lub kill).


### B. Synchronizacja procesów i pamięć dzielona (Rejestracja)

* **Pamięć dzielona (StanSOR):** Struktura StanSOR przechowuje kluczowe zmienne: dlugosc_kolejki_rejestracji oraz flagę czy_okienko_2_otwarte. Licznik kolejki jest inkrementowany przez proces pacjenta przy wejściu i dekrementowany po obsłudze.
  [Struktura StanSOR: wspolne.h](https://github.com/utratav/SO_SOR/blob/981ff243d3635545750d90b8f0fa4d9046ef8aea/wspolne.h#L81-L98)

* **Decyzja o otwarciu (Pacjent):** To proces Pacjenta (a nie Rejestracji) wykrywa tłok. Wchodząc do poczekalni (sekcja krytyczna SEM_DOSTEP_PAMIEC), sprawdza czy kolejka przekracza próg (MAX_PACJENTOW / 2). Jeśli tak, ustawia flagę czy_okienko_2_otwarte = 1 w pamięci dzielonej. [Logika pacjenta: pacjent.c](https://github.com/utratav/SO_SOR/blob/981ff243d3635545750d90b8f0fa4d9046ef8aea/pacjent.c#L220-L234)

* **Sterowanie Bramką (Okienko 1):** Proces pierwszego okienka pełni rolę zarządcy. Odczytuje flagę ustawioną przez pacjenta. Jeśli jest ustawiona, podnosi semafor SEM_BRAMKA_2 (operacja +1), fizycznie dopuszczając drugie okienko do pracy.
  Gdy liczba pacjentów spadnie poniżej MAX_PACJENTOW / 3, zdejmuje flagę w pamięci i wykonuje operację P (-1) na SEM_BRAMKA_2.
  [Otwieranie bramki: rejestracja.c](https://github.com/utratav/SO_SOR/blob/981ff243d3635545750d90b8f0fa4d9046ef8aea/rejestracja.c#L71-L100)

* **Praca Warunkowa (Okienko 2):** Drugie okienko w pętli wykonuje operację P (-1) na SEM_BRAMKA_2. Jeśli semafor jest podniesiony, proces przechodzi dalej. Używa flagi IPC_NOWAIT przy odbiorze wiadomości – jeśli kolejka jest pusta (brak pacjentów mimo otwarcia), oddaje semafor (+1), aby nie blokować zasobu bezczynnie. [Pętla Okienka 2: rejestracja.c](https://github.com/utratav/SO_SOR/blob/981ff243d3635545750d90b8f0fa4d9046ef8aea/rejestracja.c#L71-L100)

* **Ochrona zasobów (Mutex):** Wszelkie operacje na licznikach kolejek oraz flagach w strukturze StanSOR są otoczone semaforem SEM_DOSTEP_PAMIEC (mutex). Gwarantuje to spójność danych przy jednoczesnym dostępie wielu procesów (main, pacjent, rejestracji, lekarzy). [inicjalizacja SEM_DOSTEP_PAM: main.c](https://github.com/utratav/SO_SOR/blob/981ff243d3635545750d90b8f0fa4d9046ef8aea/main.c#L262-L264)
        

### C. Komunikacja i Kolejki Komunikatów

* **Inicjalizacja zasobów IPC:** W procesie main tworzone są kolejki komunikatów dla każdego etapu (Rejestracja, POZ, Specjaliści) oraz zestaw semaforów ID_SEM_LIMITS. Semafory te są inicjalizowane wartością INT_LIMIT_KOLEJEK, co definiuje maksymalną pojemność każdej kolejki w systemie. [Inicjalizacja w main: main.c](https://github.com/utratav/SO_SOR/blob/981ff243d3635545750d90b8f0fa4d9046ef8aea/main.c#L242-L250), [funkcja pomocnicza: main.c](https://github.com/utratav/SO_SOR/blob/981ff243d3635545750d90b8f0fa4d9046ef8aea/main.c#L214-L219
)

* **Struktura KomunikatPacjenta i rola mtype:** Wymiana danych opiera się na strukturze KomunikatPacjenta. Pole mtype pełni kluczową rolę routingową: przy wysyłaniu do lekarza oznacza priorytet (np. 1=VIP/Czerwony), natomiast przy odsyłaniu wyniku do pacjenta przyjmuje wartość jego PID, co pozwala na precyzyjne adresowanie odpowiedzi do konkretnego procesu. [Definicja struktury: wspolne.h](https://github.com/utratav/SO_SOR/blob/981ff243d3635545750d90b8f0fa4d9046ef8aea/wspolne.h#L100-L108)

* **Cykl Priorytetów i Zwrotka (VIP -> Kolor -> Skierowanie):**

  VIP: Pacjent ustawia mtype na 1 (VIP) lub 2 (Zwykły).

  Rejestracja odbiera z flagą -2 (najpierw mniejsze wartości).

  Rejestracja zwraca wiadomość do pacjenta z mtype = PID pacjenta, ustawiamy nowy mtype = 1 (do POZ nie ma priorytetu) 

  Triaż: POZ otrzymuje wysłaną wiadomość pacjenta, nadaje kolor i ustawia go jako nowy mtype (Czerwony=1, Żółty=2...). Specjalista odbiera z flagą -3.

  Decyzja: Lekarz aktualizuje pole skierowanie (Dom/Oddział) i odsyła strukturę do pacjenta (mtype = pacjent_pid).

  [msgrcv Rejestracja: rejestracja.c](https://github.com/utratav/SO_SOR/blob/981ff243d3635545750d90b8f0fa4d9046ef8aea/rejestracja.c#L114-L117), [msgrcv POZ: lekarz.c](https://github.com/utratav/SO_SOR/blob/981ff243d3635545750d90b8f0fa4d9046ef8aea/lekarz.c#L86-L93), [msgrcv wybrany Specjalista: lekarz.c](https://github.com/utratav/SO_SOR/blob/981ff243d3635545750d90b8f0fa4d9046ef8aea/lekarz.c#L211-L216)

* **Limitowanie kolejek (Semafory):** Przed wysłaniem komunikatu, pacjent wykonuje operację semop (-1) na semaforze limitu danej kolejki (np. SLIMIT_KARDIOLOG). Semafor jest zwalniany (+1) dopiero po odebraniu odpowiedzi od lekarza. Zapobiega to przepełnieniu buforów systemowych. [Blokada limitu: pacjent.c](https://github.com/utratav/SO_SOR/blob/981ff243d3635545750d90b8f0fa4d9046ef8aea/pacjent.c#L246-L261)

* **Obsługa błędów wysyłania (msgsnd):** Wysłanie wiadomości jest realizowane w pętli while, obsługa errno:

  `EINTR`: Jeśli wywołanie przerwał sygnał, pętla ponawia próbę (continue).

  `EAGAIN`: Jeśli kolejka systemowa jest pełna, proces czeka (usleep) i próbuje ponownie, zamiast kończyć się błędem.


### D. Obsługa sygnałów: Wezwanie lekarza na oddział (SIG_LEKARZ_ODDZIAL)

* **Handler (lekarz.c):** Sygnał SIGUSR2 (zdefiniowany jako SIG_LEKARZ_ODDZIAL) jest obsługiwany przez dedykowaną funkcję handle_sig - ustawienia zmienną globalną volatile int wezwanie_na_oddzial = 1. Właściwa logika wykonuje się w pętli głównej.           [Handler sygnału: lekarz.c](https://github.com/utratav/SO_SOR/blob/bc26a3ec0942c9c8b81e233bb680d9a9df01ee74/lekarz.c#L63-L74)

* **Logika opuszczenia SOR (Pętla lekarza):**  W pętli nieskończonej lekarz sprawdza flagę wezwanie_na_oddzial. Jeśli jest ustawiona:

    Blokuje dostęp do pamięci (SEM_DOSTEP_PAMIEC).

    Zmienia swój status w StanSOR na niedostępny (dostepni_specjalisci[i] = 0).

    Symuluje pobyt na oddziale (sleep(10)) - oczywiście można zmienić czas pobytu na rand() - wedle uznania.

    Po powrocie przywraca status dostępności i zeruje flagę (wezwanie_na_oddzial = 0), wznawiając przyjmowanie pacjentów.

    [Obsługa w pętli: lekarz.c](https://github.com/utratav/SO_SOR/blob/bc26a3ec0942c9c8b81e233bb680d9a9df01ee74/lekarz.c#L176-L209)

* **Proces Dyrektora (main.c):** Rolę inicjatora wezwań pełni proces potomny Dyrektora, tworzony w main. Działa on w pętli równoległej do symulacji, losowo wybierając ID lekarza i wysyłając do niego sygnał kill(pid, SIG_LEKARZ_ODDZIAL).
  
    ***[UWAGA]*** żeby odpalić proces dyrektora należy uruchomić program dodając argument `./main auto` , w przeciwnym wezwanie możemy obsłyżyć jedynie z konsoli.
    [Pętla Dyrektora: main.c](https://github.com/utratav/SO_SOR/blob/bc26a3ec0942c9c8b81e233bb680d9a9df01ee74/main.c#L309-L337)



### E. Obsługa sygnałów: Procedura Ewakuacji (SIG_EWAKUACJA)

* **Inicjalizacja i Koordynacja (Main):** Proces główny ignoruje sygnał SIG_EWAKUACJA, aby nie ulec zamknięciu. W przypadku przerwania (np. SIGINT), funkcja signal_handler sprawdza flagę sprzatanie_trwa (aby uniknąć podwójnego wywołania), a następnie uruchamia funkcję przeprowadz_ewakuacje(), która wysyła sygnał do całej grupy procesów: kill(0, SIG_EWAKUACJA). Po wysłaniu sygnału, main wykonuje pętlę while(wait(NULL) > 0). Gwarantuje to, że symulacja nie zakończy się, dopóki każdy proces (pacjent, lekarz) nie obsłuży sygnału i bezpiecznie nie zamknie się (exit), co zapobiega powstawaniu procesów zombie. [funkcja pomocniacza: main.c](https://github.com/utratav/SO_SOR/blob/bc26a3ec0942c9c8b81e233bb680d9a9df01ee74/main.c#L26-L52), [sig_handler: main.c](https://github.com/utratav/SO_SOR/blob/bc26a3ec0942c9c8b81e233bb680d9a9df01ee74/main.c#L196-L212)

* **Handler Pacjenta:** Proces pacjenta w handlerze wchodzi do sekcji krytycznej (Mutex SEM_DOSTEP_PAMIEC). Aktualizuje globalny stan StanSOR: zmniejsza licznik osób w środku oraz inkrementuje licznik ewakuowani. Pozwala to później zweryfikować, czy liczba osób, które uciekły, zgadza się z liczbą osób, które były w środku. Przed ostatecznym wyjściem, pacjent "oddaje" zajęte miejsca w semaforze wejściowym SEM_MIEJSCA_SOR (operacja +1 lub +2). Jest to niezbędne, aby stan semaforów systemowych pozostał spójny do samego końca działania programu. W przypadku pacjentów małoletnich, handler ewakuacji dba również o dołączenie wątku opiekuna (pthread_join) przed zakończeniem procesu, co zapewnia poprawne zwolnienie wszystkich zasobów wątkowych.
[ewakuacja pacjenta: main.c](https://github.com/utratav/SO_SOR/blob/bc26a3ec0942c9c8b81e233bb680d9a9df01ee74/pacjent.c#L26-L65)

* **Pozostałe procesy** przy wywyłaniu, wykonują exit(0)

* **Raportowanie i Sprzątanie:** Na końcu procedury wywoływane są funkcje pomocnicze podsumowanie() (wyświetlenie statystyk) oraz czyszczenie() (usunięcie kolejek, pamięci i semaforów), które finalizują działanie symulacji.


### F. Wątki i Monitory Systemowe

* **Symulacja Opiekuna (Pacjent):** W procesie pacjenta, jeśli wylosowany wiek jest mniejszy niż 18 lat, tworzony jest dodatkowy wątek za pomocą pthread_create. Reprezentuje on fizyczną obecność opiekuna. Funkcja wątku watek_rodzic jest pusta, ale samo istnienie wątku wpływa na logikę semaforów – proces zajmuje dwa miejsca w poczekalni (SEM_MIEJSCA_SOR -2) zamiast jednego. [Tworzenie opiekuna i logika semop: pacjent.c](https://github.com/utratav/SO_SOR/blob/0aeb05a7a073ffc0d7131347e9924cef7661ff39/pacjent.c#L161-L201)

* **Monitor Kolejek (Main):** Wątek watek_monitor_kolejki uruchamiany w main działa w tle przez cały czas trwania symulacji. Jego zadaniem jest bezinwazyjny odczyt stanu systemu: używa semctl do sprawdzenia limitów (wolne miejsca w kolejkach) oraz msgctl z flagą IPC_STAT do pobrania faktycznej liczby oczekujących komunikatów. Dane te są logowane do pliku ***raport1.txt***. [Logika monitora kolejek: main.c](https://github.com/utratav/SO_SOR/blob/0aeb05a7a073ffc0d7131347e9924cef7661ff39/main.c#L54-L114)

* **Monitor Bramki (Main):** Drugi wątek pomocniczy, watek_monitor_bramki, śledzi dynamikę rejestracji z częstotliwością 200ms. Podłącza się do pamięci dzielonej StanSOR i rejestruje w pliku ***raport2.txt*** każdą zmianę statusu drugiego okienka oraz generuje "ALARM", jeśli długość kolejki jest nieadekwatna do stanu bramek (np. duża kolejka przy zamkniętym okienku). [Logika monitora bramki: main.c](https://github.com/utratav/SO_SOR/blob/0aeb05a7a073ffc0d7131347e9924cef7661ff39/main.c#L54-L114)

* **Zarządzanie cyklem życia wątków:** Wszystkie wątki są tworzone funkcją pthread_create na początku działania programów. Wykonanie ich pętli głoównych jest uwarunkowane zmienną volatile int monitor_running = 1; Ich poprawne zamknięcie jest gwarantowane przez pthread_join: w main.c następuje to podczas obsługi sygnału SIGINT (Ctrl+C) lub końca symulacji - ustawiamy flage monitor_running na 0, a w pacjent.c – przed ewakuacją lub normalnym wyjściem. Zapobiega to wyciekom zasobów. 



### G. Inicjalizacja systemu i tworzenie procesów (Main)

* **Tworzenie środowiska IPC:** Na początku funkcji main generowane są unikalne klucze (ftok). Następnie tworzone są kolejki komunikatów (w pętli dla wszystkich typów lekarzy), blok pamięci dzielonej (shmget) oraz zbiory semaforów. Pamięć dzielona jest natychmiast przyłączana (shmat) i zerowana (memset), aby zapewnić czysty start symulacji. [Alokacja zasobów: main.c](https://github.com/utratav/SO_SOR/blob/0aeb05a7a073ffc0d7131347e9924cef7661ff39/main.c#L238-L274), [pomocnicze msg_create: main.c](https://github.com/utratav/SO_SOR/blob/0aeb05a7a073ffc0d7131347e9924cef7661ff39/main.c#L221-L231)

* **Konfiguracja Semaforów (semctl):** Semafory są inicjalizowane konkretnymi wartościami startowymi za pomocą polecenia SETVAL. Mutexy (np. SEM_DOSTEP_PAMIEC) otrzymują wartość 1 (otwarte), semafor wejściowy SEM_MIEJSCA_SOR wartość MAX_PACJENTOW, a bramka drugiego okienka wartość 0 (zamknięte). Osobny zestaw semaforów limitujących kolejki jest ustawiany grupowo (SETALL). [Ustawianie wartości: main.c](https://github.com/utratav/SO_SOR/blob/0aeb05a7a073ffc0d7131347e9924cef7661ff39/main.c#L262-L274)

* **Wrapper uruchom_proces i użycie execl:** zaimplementowano funkcję pomocniczą uruchom_proces. Standaryzuje ona tworzenie potomka i obsługę błędów. Wewnątrz niej wywoływana jest funkcja execl, która podmienia obraz procesu na właściwy program (np. ./lekarz). Parametry konfiguracyjne (np. ID specjalizacji, numer okienka) są przekazywane jako argumenty wywołania (argv). [Implementacja wrappera: main.c](https://github.com/utratav/SO_SOR/blob/0aeb05a7a073ffc0d7131347e9924cef7661ff39/main.c#L262-L274),[Przykład użycia wrappera](https://github.com/utratav/SO_SOR/blob/0aeb05a7a073ffc0d7131347e9924cef7661ff39/main.c#L278-L280)

* **Struktura Procesów:** Proces main sekwencyjnie uruchamia całą kadrę: 2 procesy rejestracji, lekarza POZ, 6 specjalistów oraz na końcu generator pacjentów. Wszystkie te procesy są bezpośrednimi dziećmi procesu głównego, co ułatwia zarządzanie nimi przy ewakuacji.

### H. Funkcje pomocnicze: Raportowanie i Czyszczenie zasobów

* **(zapisz_raport):** Zdefiniowana w wspolne.h funkcja zapisz_raport obsługuje wyjście sformatowane (użycie va_list i vfprintf). Kluczowym elementem jest użycie semafora SEM_ZAPIS_PLIK jako mutexa otaczającego operację wyjścia stdout. Gwarantuje to, że   komunikaty z różnych procesów (np. Rejestracji i Lekarza) nie nakładają się na siebie w konsoli, jako pierwszy argument przyjmuje plik wyjścia - zapisując do pliku raport*.txt - nie korzystamy z semafora.
  [Implementacja funkcji: wspolne.h](https://github.com/utratav/SO_SOR/blob/0aeb05a7a073ffc0d7131347e9924cef7661ff39/wspolne.h#L117-L150)

* **Statystyka końcowa (podsumowanie):** Funkcja ta analizuje dane zgromadzone w strukturze pamięci dzielonej StanSOR. Porównuje rzeczywistą liczbę obsłużonych pacjentów (VIP, kolory triażu, decyzje o hospitalizacji) z wartościami oczekiwanymi (np. 10% czerwonych, 85% do domu), wyświetlając weryfikację poprawności symulacji względem założeń projektowych. [Logika statystyk: wspolne.h](https://github.com/utratav/SO_SOR/blob/0aeb05a7a073ffc0d7131347e9924cef7661ff39/wspolne.h#L152-L199)

* **Zwalnianie zasobów (czyszczenie):** Procedura czyszczenie w main.c jest "destruktorem" systemu. Wywoływana przy normalnym końcu pracy lub po przechwyceniu sygnału SIGINT, usuwa z systemu operacyjnego wszystkie utworzone obiekty IPC (kolejki msgctl, semafory semctl, pamięć shmctl z flagą IPC_RMID). [Procedura sprzątająca: main.c](https://github.com/utratav/SO_SOR/blob/0aeb05a7a073ffc0d7131347e9924cef7661ff39/main.c#L175-L194)

---


## Podręcznik do semaforów

| Zbiór (klucz) | Nazwa | Wartość Początkowa | Opis działania |
| :--- | :--- | :--- | :--- |
| **ID_SEM_SET** | **SEM_DOSTEP_PAMIEC** | **1** | Mutex Pamięci. Chroni strukturę StanSOR przed jednoczesnym zapisem (wyścigami). |
| **ID_SEM_SET** | **SEM_MIEJSCA_SOR** | **MAX_PACJENTOW** | Licznik Miejsc. Blokuje wejście pacjentów przy pełnym obłożeniu.|
| **ID_SEM_SET** | **SEM_ZAPIS_PLIK** | **1** | Mutex I/O. Synchronizuje dostęp do konsoli, zapobiegając nakładaniu się tekstów. |
| **ID_SEM_SET** | **SEM_GENERATOR** | **MAX_PROCESOW** | Limit Procesów. Ogranicza liczbę jednocześnie działających procesów pacjentów (chroni przed fork bombą). |
| **ID_SEM_SET** | **SEM_BRAMKA_2** | **0** | Sterowanie Okienkiem 2. Wartość 0 usypia drugie okienko rejestracji, wartość 1 pozwala mu pracować. |
| **ID_SEM_LIMITS** | **SLIMIT_... [0-7]** | **INT_LIMIT_KOLEJEK** | Zbiór 8 semaforów. Każdy indeks odpowiada jednej kolejce komunikatów (Rejestracja, POZ, Specjaliści). Działają jako ograniczenie producent-konsument dla systemowych buforów IPC. |




---

## Testy

### Test 1: Skalowalność Rejestracji (Dynamiczna Bramka nr 2)

* **Konfiguracja testowa:** Zgodnie z załączonym kodem, dla celów testowych ustawiono zwiększone limity, aby wygenerować duży ruch:

    `MAX_PACJENTOW` = 1000 (Pojemność SOR)

    `PACJENCI_NA_DOBE` = 50000 

    Próg otwarcia (N/2): > 500 osób.

    Próg zamknięcia (N/3): <= 333 osób. Definicje makr: wspolne.h
  


  <img width="677" height="601" alt="Zrzut ekranu 2026-01-28 143227" src="https://github.com/user-attachments/assets/4b5ae3c5-bf47-42bd-b67c-5496bc20bc05" />

  

* **Weryfikacja Otwarcia:** Analiza pliku raport2.txt pokazuje linię: [REJESTRACJA] Otwieram 2 okienko | osob w kolejce: 501 Mechanizm zadziałał poprawnie – flaga została ustawiona przez pacjenta w momencie, gdy licznik przekroczył próg 500 (wynosił 501), co skutkowało podniesieniem semafora bramki. 

* **Weryfikacja Zamknięcia:** W dalszej części logu widzimy linię: [REJESTRACJA] Zamykam 2 okienko | osob w kolejce: 333 System poprawnie utrzymał drugie okienko otwarte mimo spadku poniżej 500. Zamknięcie nastąpiło dopiero po osiągnięciu dolnego progu (333).


* **Wniosek:** Mechanizm synchronizacji między procesem Pacjenta (detekcja tłoku) a procesem Rejestracji 1 (sterowanie semaforem bramki) działa zgodnie z założeniami projektowymi, dynamicznie dostosowując przepustowość do obciążenia.




### Test 2: Weryfikacja statystyczna i spójność danych

<img width="475" height="560" alt="Zrzut ekranu 2026-01-28 145931" src="https://github.com/user-attachments/assets/63a131d6-dc4d-4fcc-afeb-742aab6747ce" />

* Test potwierdza matematyczną szczelność systemu dla próby 50 000 pacjentów.

* **Poprawność sumaryczna:** Wygenerowano 10 110 VIP-ów i 39 890 pacjentów zwykłych, co sumuje się idealnie do 50 000 i pokrywa z założonym 20% prawdopodobieństwem wystąpienia VIP-a.

* **Triaż i POZ:** Suma pacjentów z nadanym kolorem (47 477) jest mniejsza od całkowitej o 2 523. Różnica ta odpowiada dokładnie liczbie pacjentów zdrowych odesłanych do domu bezpośrednio przez POZ, co potwierdza poprawność logiki "luki triażowej".

* **Rozkład priorytetów:** Liczebności grup priorytetowych (Czerwony ~10%, Żółty ~35%, Zielony ~50%) odpowiadają wagom zdefiniowanym w algorytmie losującym.

* **Bilans przepływu:** Suma pacjentów obsłużonych przez specjalistów (47 477) równa się liczbie nadanych kolorów, a suma wszystkich decyzji wyjściowych (Dom/Oddział/Inne) wynosi 50 000. Oznacza to, że żaden proces nie został zgubiony w trakcie symulacji.
