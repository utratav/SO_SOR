# Projekt Systemy Operacyjne – Temat 8: SOR

**Autor:** Hugo Utrata
**Numer albumu:** 155238
**Temat:** 8 – SOR

---

## Krótki opis projektu

Projekt realizuje symulację działania Szpitalnego Oddziału Ratunkowego (SOR) z uwzględnieniem rzeczywistych zasad organizacji pracy. System odtwarza przebieg wizyty pacjenta: wejście na SOR (z limitem miejsc), rejestrację, ocenę stanu zdrowia w triażu oraz konsultację u właściwego lekarza specjalisty. Pacjenci mają różne priorytety (czerwony, żółty, zielony) wpływające na kolejność obsługi, a niektórzy mogą być wysyłani do domu już na etapie triażu. Lekarze mogą kierować pacjentów do dalszego leczenia, wypisywać ich do domu lub odsyłać do innej placówki. Symulacja obejmuje również obsługę pacjentów VIP oraz reakcję całego systemu na sygnały Dyrektora (np. przerwanie pracy lekarza, natychmiastowa ewakuacja SOR). Całość realizowana jest za pomocą procesów, pamięci dzielonej, semaforów i sygnałów systemowych, zgodnie z wymaganiami zajęć z systemów operacyjnych.

---

## Wytyczne projektu – Temat 8: SOR

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

#### A. Rejestracja:

− Pacjent podaje swoje dane i opisuje objawy.

#### B. Ocena stanu zdrowia (Triaż):

− Lekarz POZ weryfikuje stan pacjenta i przypisuje mu kolor zgodny z pilnością udzielenia pomocy (na tej podstawie określa się, kto otrzyma pomoc w pierwszej kolejności):

* **czerwony** – natychmiastowa pomoc – ok. 10% pacjentów;
* **żółty** – przypadek pilny – ok. 35% pacjentów;
* **zielony** – przypadek stabilny – ok. 50% pacjentów;
  − Ok. 5% pacjentów jest odsyłanych do domu bezpośrednio z triażu;
  − Lekarz POZ po przypisaniu koloru, kieruje danego pacjenta do lekarza specjalisty: kardiologa, neurologa, okulisty, laryngologa, chirurga, pediatry.

#### C. Wstępna diagnostyka i leczenie:

− Lekarz specjalista wykonuje niezbędne badania (wywiad, badanie fizykalne, EKG, pomiar ciśnienia, …), aby ustabilizować funkcje życiowe pacjenta.

#### D. Decyzja o dalszym postępowaniu:

− Po wstępnej diagnozie i stabilizacji stanu pacjent może zostać przez lekarza specjalistę:

* wypisany do domu – ok. 85% pacjentów;
* skierowany na dalsze leczenie do odpowiedniego oddziału szpitalnego – ok. 14.5% pacjentów;
* skierowany do innej, specjalistycznej placówki – ok. 0,5% pacjentów.

### Sygnały Dyrektora:

* **Sygnał 1** – dany lekarz specjalista bada bieżącego pacjenta i przerywa pracę na SOR-rze i udaje się na oddział. Wraca po określonym losowo czasie.
* **Sygnał 2** – wszyscy pacjenci i lekarze natychmiast opuszczają budynek.

### Wymagane moduły:

Należy zaimplementować procesy:

* **Dyrektor**
* **Rejestracja**
* **Lekarz (POZ oraz specjaliści)**
* **Pacjent**

Raport z przebiegu symulacji musi być zapisany w pliku tekstowym.

---

*https://github.com/utratav/SO_SOR.git*

