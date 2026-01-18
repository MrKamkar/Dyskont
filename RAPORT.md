# Temat 16 - Dyskont [Raport Projektu]

**Kamil Karpiel**  
Grupa 1 | Studia stacjonarne | III semestr | Nr albumu 155184

## 1. Założenia projektu

Celem projektu było stworzenie symulacji działania **dyskontu**, opartej na **architekturze wieloprocesowej** (proces: _klienta_, _pracownika_, _kasjera_, _kasy samoobsługowej_ oraz sam _dyskontu_), a także na **wątkach** (_logów_ podpiętych do procesu głównego).
Komunikacja pomiędzy procesami jak i wątkami została zrealizowana z wykorzystaniem szeregu różnych **mechanizmów komunikacji międzyprocesowej (IPC)** wbudowanych w **system Linux**.

Szczegółowe wymagania dotyczące realizacji projektu znajdują się w pliku: https://github.com/MrKamkar/Dyskont/blob/main/README.md

## 2. Ogólny opis plików

Projekt został podzielony na moduły funkcjonalne:

- **main.c**: Główny proces realizujący przebieg całej symulacji. Inicjalizuje zasoby IPC (pamięć, semafory, kolejki), tworzy procesy potomne (pracownicy, kasjerzy, kasy samoobsługowe) oraz generuje klientów w pętli symulacyjnej. Obsługuje również sygnały dostane od kierownika i wyświetla logi zapisywane do oddzielnego pliku.

- **pamiec_wspoldzielona.c / .h**: Zawiera definicję struktury `StanSklepu` oraz funkcje zarządzające segmentem pamięci współdzielonej, umożliwiając współdzielenie danych z innymi procesami.

- **semafory.c / .h**: Biblioteka pomocnicza upraszczająca operacje na semaforach Systemu V. Implementuje mechanizmy synchronizacji takich jak: mutexy, semafory licznikowe niezbędne do ochrony sekcji krytycznych.

- **logi.c / .h**: Wątek odpowiedzialny za logowanie zdarzeń. Wykorzystuje nieblokującą kolejkę komunikatów, co zapobiega blokowaniu głównej pętli podczas wykonywania symulacji.

- **klient.c / .h**: Proces reprezentujący przebieg działań podejmowanych przez klienta podczas zakupów. Można go uruchomić w trybie standalone, by uzyskać wydrukowany paragon z symulacji. Logika klienta symuluje proces kompletowania koszyka (wybór losowych produktów), strategię wyboru kasy (samoobsługowa lub najkrótsza stacjonarna) oraz interakcję z innymi procesami w celu obsługi.

- **kasjer.c / .h**: Proces obsługujący działanie kasy stacjonarnej. Realizuje obsługę klientów oczekujących w kolejce (pobieranie komunikatów IPC), symuluje czas skanowania produktów oraz zarządza stanem kasy w reakcji na polecenia sterujące od kierownika lub na podstawie ilości klientów w kolejce do kasy 1.

- **kasa_samoobslugowa.c / .h**: Proces symulujący działanie stanowiska samoobsługowego, pozwalający klientom na samodzielne dokonanie płatności. Symuluje losowe blokady (np. niezgodność wagi) oraz wzywa pracownika obsługi do weryfikacji wieku lub odblokowania kasy.

- **kierownik.c / .h**: Proces dający użytkownikowi możliwość kontroli nad działaniem dyskontu. Umożliwia dynamiczne zarządzanie sklepem poprzez wysyłanie sygnałów do procesu głównego (otwieranie/zamykanie kas, zarządzenie ewakuacją) oraz podgląd aktualnego stanu systemu.

- **pracownik_obslugi.c / .h**: Proces pomocniczy reagujący na wezwania z kas samoobsługowych. Obsługuje weryfikację wieku klientów kupujących alkohol oraz zdejmuje blokady techniczne kas, jeśli jest to zgodne z regulaminem.

- **wspolne.c / .h**: Biblioteka zawierająca definicje stałych, struktur (np. komunikatów IPC) oraz funkcji pomocniczych używanych przez wszystkie moduły (generowanie kluczy IPC, obsługa sygnałów wyjścia, inicjalizacja procesu pochodnego).

### 3. Co udało się zrealizować?

- **Pełna symulacja wieloprocesowa**: Udalo się stworzyć stabilnie działający system symulujący pracę dyskontu, w którym każdy podmiot (kasjer, klient, kierownik, kasa samoobsługowa) jest osobnym procesem.

- **Bezpieczna obsługa sygnałów**: Wdrożono zaawansowany mechanizm obsługi sygnałów, gdzie handler jedynie zapisuje bajt do potoku, a właściwa logika (np. sprzątanie procesów potomnych) wykonywana jest w pętli głównej.

- **Wątek loggera**: Zastosowanie osobnego wątku do logowania operacji (z kolejką komunikatów) znacząco odciążyło główne procesy, eliminując opóźnienia związane z operacjami wejścia/wyjścia.

- **Inteligentne zarządzanie kolejkami**: Klienci dynamicznie wybierają kasę (krótsza kolejka, stan kasy) oraz potrafią migrować do nowo otwartej kasy na sygnał kierownika.

- **Bezpieczne zamykanie aplikacji i czyszczenie zasobów IPC**: System poprawnie reaguje na sygnały różne zamykania aplikacji, rozpoczynając procedurę ewakuacji klientów, a następnie bezpiecznie kończy wszystkie procesy potomne i zwalnia zasoby IPC.

### 4. Z czym były problemy?

- **Procesy zombie**: Występował problem z niepoprawnym zliczaniem procesów potomnych. Rozwiązano to poprzez użycie `waitpid` w pętli z flagą `WNOHANG`, która umożliwia bezblokowe posprzątanie wszystkich zakończonych dzieci naraz w handlerze `SIGCHLD`, oraz na komunikację przez pipe z pętlą główną.

- **Deadlocki**: Zdarzały się, gdy procesy blokowały się na operacjach IPC (semafory, odbieranie komunikatów) w trakcie najczęściej niepoprawnego zamykania aplikacji. Rozwiązano to poprzez zastosowanie **mechanizmu synchronizacji sygnałów przez potok** do bezpiecznej obsługi sygnałów oraz wdrożenie procedury, która przy zamykaniu aplikacji aktywnie wybudza wszystkie procesy oczekujące na zasoby.

- **Problemy z synchronizacją**: Początkowo projekt opierał się na kolejkach zaimplementowanych ręcznie w pamięci współdzielonej i semaforach, co prowadziło do trudnych asynchronicznych blędów w odczycie/zapisie kolejek. Problem rozwiązano poprzez całkowite przejście na **kolejki komunikatów**, które zapewniają spójność operacji na danych.

- **Aktywne oczekiwanie (Polling)**: Wstępna wersja projektu wykorzystywała pętle z `usleep()`, co niepotrzebnie obciążało procesor. Cały kod został przebudowany na **metody blokujące** (operacje na semaforach, oczekiwanie na komunikat w kolejce, odczyt z potoku), dzięki czemu procesy czekają na zdarzenie, zamiast aktywnie sprawdzać ich stan.

### 4 Mechanizmy synchronizacji i IPC

**- Pamięć dzielona (`shmget`/ `shmat`):**
Obiekt `StanSklepu` jest mapowany przez każdy proces. Zawiera:

- Tablice struktur `Kasa` (statusy, liczniki kolejek).
- Liczniki globalne (liczba klientów w sklepie, flagi ewakuacji).
- Magazyn produktów.

**- Semafory (`semop`):**
Zestaw semaforów kontroluje dostęp do zasobów, zapobiegając niesynchronizowanemu dostępowi do danych:

- `MUTEX_PAMIEC_WSPOLDZIELONA`: Chroni modyfikację stanu sklepu (blokuje dostęp do pamięci współdzielonej).
- `SEM_OTWORZ_KASA_STACJONARNA_X`: Sygnalizuje kasjerom polecenie otwarcia kasy.

**- Kolejki komunikatów (`msgrcv`/`msgsnd`):**

Umożliwiają dwukierunkową wymianę danych między procesami:

- **Adresowanie komunikatów:** Kasjerzy odbierają tylko wiadomości skierowane do nich (typ `ID_KASY + 1`, bo mtype > 0), a kasy samoobsługowe korzystają ze wspólnego kanału (typ `MSG_TYPE_SAMOOBSLUGA`).

- **Informacja zwrotna:** Klient nasłuchuje na specyficznym typie wiadomości (`BASE + własne_ID`), co pozwala mu wyłowić z kolejki tylko swój paragon. W odpowiedzi zwrotnej, zamiast (zbędnej dla klienta) liczby produktów, w polu `liczba_produktow` przesyłane jest ID kasy, która go obsłużyła. Dzięki temu unika się powiększania struktury komunikatu.

- **Logowanie nieblokujące:** Procesy wysyłają logi do kolejki loggera z flagą `IPC_NOWAIT`, dzięki czemu symulacja nie zatrzymuje się w oczekiwaniu na zapis na dysk.

**- Potoki nienazwane (`pipe`):**
Wykorzystane do bezpiecznej obsługi sygnałów systemowych:

- **Problem:** Wiele funkcji systemowych nie może być bezpiecznie wywoływanych wewnątrz funkcji obsługi sygnałów (handlerów).
- **Rozwiązanie:** Handler sygnału jedynie zapisuje pojedynczy bajt do potoku. Pętla główna programu odczytuje ten bajt w bezpiecznym momencie i dopiero wtedy wykonuje właściwe operacje (np. sprzątanie po zakończonym procesie).

## 5. Kluczowe Pseudokody

### 1. main.c (Proces Główny)

```
Inicjalizacja:
    utworz_pamiec_wspoldzielona()
    utworz_semafory()
    utworz_kolejke_komunikatow()
    uruchom_watek_loggera()

Uruchamianie podprocesów:
    FOR i = 0 TO LICZBA_KAS_STACJONARNYCH:
        fork() -> exec("kasjer")
    FOR i = 0 TO LICZBA_KAS_SAMOOBSLUGOWYCH:
        fork() -> exec("kasa_samo")
    fork() -> exec("pracownik")

Pętla symulacji:
    WHILE czas_trwania < limit I brak_ewakuacji:
        aktualizuj_czas()
        zarzadzaj_otwarciem_kas_samoobslugowych()

        IF liczba_klientow < MAX:
            fork() -> exec("klient")
            zwieksz_licznik_klientow()

        sleep(czas_miedzy_klientami)
        obsluz_sygnaly_IPC()

Sprzątanie:
    wyslij_sygnal_ewakuacji_do_dzieci()
    wait_for_all_children()
    usun_zasoby_IPC()
```

### 2. klient.c

```
Start:
    dolacz_do_pamieci_i_kolejki()
    wylosuj_koszyk_produktow()

Zakupy:
    WHILE koszyk_niepelny I brak_ewakuacji:
        sleep(czas_wyboru)
        wybierz_produkt()
        dodaj_do_koszyka()

Wybór kasy:
    IF ma_alkohol I wiek < 18:
        odmowa_zakupu()

    IF losuj(kasa_samoobslugowa):
        wyslij_msg(MSG_SAMOOBSLUGA, koszyk)
        czekaj_na_odpowiedz()
        IF timeout LUB blad:
            przejdz_do_kasy_stacjonarnej()

    IF idzie_do_stacjonarnej:
        wybierz_najkrótsza_kolejke(kasa1, kasa2)
        wyslij_msg(MSG_KASA_X, koszyk)
        czekaj_na_odpowiedz(MSG_RES_X)

Koniec:
    odejmij_sie_z_licznika_sklepu()
    wyjdz()
```

### 3. kasjer.c

```
Pętla główna:
    WHILE TRUE:
        sprawdz_stan_kasy() (OTWARTA/ZAMKNIETA)

        IF stan == ZAMKNIETA:
            czekaj_na_semaforze(OTWORZ_KASE)

        IF polecenie_kierownika == ZAMKNIJ:
            ustaw_stan(ZAMYKANA)

        odbierz_klienta(msgrcv)

        obsluga:
            zablokuj_dostęp_do_kasy()
            FOR produkt in koszyk:
                sleep(czas_skanowania)
            odblokuj_kase()

        wyslij_potwierdzenie(msgsnd)
```

### 4. kasa_samoobslugowa.c

```
Pętla obsługi:
    WHILE TRUE:
        IF stan == ZAMKNIETA:
            czekaj_na_aktywacje()

        odbierz_zgloszenie(msgrcv, MSG_SAMOOBSLUGA)

        Skanowanie:
            FOR produkt in koszyk:
                sleep(czas_skanowania)
                IF losuj_blokade():
                    wezwij_pracownika(OP_ODBLOKUJ)
                    czekaj_na_pomoc()

        Weryfikacja:
            IF ma_alkohol:
                wynik = wezwij_pracownika(OP_WERYFIKACJA, wiek)
                IF wynik == NEGATYWNY:
                    odrzuc_klienta()

        wyslij_potwierdzenie(msgsnd)
```

## 6. Wykonane testy

Poniżej przedstawiono zbiór testów weryfikujących poprawność działania symulacji w różnych warunkach.

### Test 1 – Klient bez alkoholu

- **Opis**: Klient kupuje produkty i w koszyku nie ma alkoholu.
- **Oczekiwany wynik**: Klient udaje się do kasy samoobsługowej (jeśli tak wylosuje), nie następuje wezwanie pracownika do weryfikacji wieku. Transakcja przebiega szybko i bez zakłóceń.

### Test 2 – Klient kupuje alkohol

- **Opis**: Klient posiada w koszyku przynajmniej jeden produkt oznaczony jako alkohol.
- **Oczekiwany wynik**: System wykrywa alkohol. Jeśli klient jest przy kasie samoobsługowej, następuje blokada i wezwanie pracownika. Pracownik sprawdza wiek – jeśli klient < 18 lat, to zakupy są anulowane. W przeciwnym razie kasa jest odblokowywana.

### Test 3 – Zbyt długie oczekiwanie (Timeout)

- **Opis**: Klient czeka w kolejce do kas samoobsługowych dłużej niż założony limit czasu (np. z powodu zapełnienia kolejek do kas samoobsługowych).
- **Oczekiwany wynik**: Klient rezygnuje z oczekiwania na kasę samoobsługową i wybiera kase stacjonarną z najkrótszą kolejką (o ile jest otwarta).

### Test 4 – Awaria kasy samoobsługowej

- **Opis**: Podczas skanowania produktów kasa samoobsługowa ulega awarii.
- **Oczekiwany wynik**: Stanowisko zostaje zablokowane. Wysyłane jest wezwanie do pracownika obsługi. Klient czeka na interwencję. Pracownik podchodzi, usuwa usterkę, a proces zakupowy jest wznawiany od miejsca przerwania.

### Test 5 – Tryb bez opóźnień (usleepów)

- **Opis**: Uruchomienie symulacji z flagą trybu testowego (`./dyskont 100 1 80`), która eliminuje opóźnienia `usleep()` w działaniu kasjerów i klientów.
- **Oczekiwany wynik**: System przetwarza setki klientów w ułamku sekundy. Test weryfikuje poprawność synchronizacji (brak deadlocków lub innych błędów) przy maksymalnej prędkości symulacji.

## 7. Linki do kodu (Wymagane funkcje systemowe)

Poniżej znajdują się odniesienia do kluczowych mechanizmów systemowych wykorzystanych w projekcie:

**a. Tworzenie i obsługa plików (`open`, `close`, `read`, `write`, `pipe`)**

- `open` (otwarcie pliku logów): [logi.c:37](https://github.com/MrKamkar/Dyskont/blob/main/Dyskont/logi.c#L37)
- `write` (zapis do logu): [logi.c:103](https://github.com/MrKamkar/Dyskont/blob/main/Dyskont/logi.c#L103)
- `close` (zamknięcie pliku): [logi.c:109](https://github.com/MrKamkar/Dyskont/blob/main/Dyskont/logi.c#L109)
- `read` (odczyt z potoku sterującego): [main.c:38](https://github.com/MrKamkar/Dyskont/blob/main/Dyskont/main.c#L38)
- `unlink` (nie użyto - pliki tymczasowe nie były wymagane, logi są trwałe).

**b. Tworzenie procesów (`fork`, `exec`, `exit`, `wait`)**

- `fork` (tworzenie procesu kasjera): [main.c:281](https://github.com/MrKamkar/Dyskont/blob/main/Dyskont/main.c#L281)
- `exec` (`execl` - nadpisanie obrazu procesu): [main.c:294](https://github.com/MrKamkar/Dyskont/blob/main/Dyskont/main.c#L294)
- `exit` (zakończenie procesu potomnego): [main.c:297](https://github.com/MrKamkar/Dyskont/blob/main/Dyskont/main.c#L297)
- `wait` (`waitpid` - oczekiwanie na dzieci): [main.c:79](https://github.com/MrKamkar/Dyskont/blob/main/Dyskont/main.c#L79)

**c. Tworzenie i obsługa wątków (`pthread_create`, `pthread_exit`)**

- `pthread_create` (utworzenie wątku loggera): [logi.c:130](https://github.com/MrKamkar/Dyskont/blob/main/Dyskont/logi.c#L130)
- `pthread_exit` (zakończenie wątku): [logi.c:110](https://github.com/MrKamkar/Dyskont/blob/main/Dyskont/logi.c#L110)
- `pthread_sigmask` (blokowanie sygnałów w wątku): [logi.c:21](https://github.com/MrKamkar/Dyskont/blob/main/Dyskont/logi.c#L21)
- _Mutexy wątków nie były wymagane, gdyż wątek loggera używa kolejki komunikatów jako bufora synchronizującego._

**d. Obsługa sygnałów (`kill`, `signal`, `sigaction`)**

- `sigaction` (rejestracja handlera SIGCHLD): [main.c:221](https://github.com/MrKamkar/Dyskont/blob/main/Dyskont/main.c#L221)
- `signal` (prosta rejestracja handlerów): [main.c:223](https://github.com/MrKamkar/Dyskont/blob/main/Dyskont/main.c#L223)
- `kill` (wysłanie sygnału do grupy procesów): [main.c:429](https://github.com/MrKamkar/Dyskont/blob/main/Dyskont/main.c#L429)

**e. Synchronizacja procesów (Semafory Systemu V)**

- `semget` (utworzenie/pobranie zestawu semaforów): [semafory.c:30](https://github.com/MrKamkar/Dyskont/blob/main/Dyskont/semafory.c#L30)
- `semop` (operacja na semaforze - wait/signal): [semafory.c:14](https://github.com/MrKamkar/Dyskont/blob/main/Dyskont/semafory.c#L14)
- `semctl` (ustawienie wartości/usunięcie): [semafory.c:42](https://github.com/MrKamkar/Dyskont/blob/main/Dyskont/semafory.c#L42)

**f. Łącza nazwane i nienazwane (`pipe`)**

- `pipe` (tworzenie potoku nienazwanego): [main.c:164](https://github.com/MrKamkar/Dyskont/blob/main/Dyskont/main.c#L164)

**g. Segmenty pamięci dzielonej (`shmget`, `shmat`, `shmdt`)**

- `shmget` (alokacja segmentu pamięci): [pamiec_wspoldzielona.c:25](https://github.com/MrKamkar/Dyskont/blob/main/Dyskont/pamiec_wspoldzielona.c#L25)
- `shmat` (dołączenie segmentu do przestrzeni adresowej): [pamiec_wspoldzielona.c:34](https://github.com/MrKamkar/Dyskont/blob/main/Dyskont/pamiec_wspoldzielona.c#L34)
- `shmdt` (odłączenie segmentu): [pamiec_wspoldzielona.c:51](https://github.com/MrKamkar/Dyskont/blob/main/Dyskont/pamiec_wspoldzielona.c#L51)

**h. Kolejki komunikatów (`msgget`, `msgsnd`, `msgrcv`)**

- `msgget` (utworzenie kolejki): [main.c:256](https://github.com/MrKamkar/Dyskont/blob/main/Dyskont/main.c#L256)
- `msgsnd` (wysłanie komunikatu): [pracownik_obslugi.c:27](https://github.com/MrKamkar/Dyskont/blob/main/Dyskont/pracownik_obslugi.c#L27)
- `msgrcv` (odebranie komunikatu): [pracownik_obslugi.c:34](https://github.com/MrKamkar/Dyskont/blob/main/Dyskont/pracownik_obslugi.c#L34)