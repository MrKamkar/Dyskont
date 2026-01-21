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

- **kasjer.c / .h**: Proces obsługujący działanie kasy stacjonarnej. Każdy kasjer używa osobnej kolejki komunikatów (`ID_IPC_KASA_1` lub `ID_IPC_KASA_2`). Realizuje obsługę klientów oczekujących w kolejce, symuluje czas skanowania produktów oraz wysyła odpowiedzi VIP.

- **kasa_samoobslugowa.c / .h**: Proces hybrydowy: główny proces działa jako kasa 0 z wątkiem skalującym, a kasy 1-5 to procesy potomne (fork). Symuluje losowe blokady (np. niezgodność wagi) oraz wzywa pracownika obsługi do weryfikacji wieku lub odblokowania kasy. Max 6 procesów (1 manager + 5 workerów).

- **kierownik.c / .h**: Proces dający użytkownikowi możliwość kontroli nad działaniem dyskontu. Umożliwia dynamiczne zarządzanie sklepem poprzez wysyłanie sygnałów do procesu głównego (otwieranie/zamykanie kas, zarządzenie ewakuacją) oraz podgląd aktualnego stanu systemu poprzez odczyt rozmiaru kolejek (`msgctl`).

- **pracownik_obslugi.c / .h**: Proces pomocniczy reagujący na wezwania z kas samoobsługowych. Obsługuje weryfikację wieku klientów kupujących alkohol oraz zdejmuje blokady techniczne kas, jeśli jest to zgodne z regulaminem.

- **wspolne.c / .h**: Biblioteka zawierająca definicje stałych, struktur (np. komunikatów IPC) oraz funkcji pomocniczych używanych przez wszystkie moduły (generowanie kluczy IPC, obsługa sygnałów wyjścia, inicjalizacja procesu pochodnego).

- **kolejki.c / .h**: Obsługa kolejek komunikatów IPC. Zawiera funkcję `WyslijKomunikatVIP` (tymczasowe podnoszenie limitu kolejki dla odpowiedzi) oraz `PobierzRozmiarKolejki` (odczyt liczby komunikatów przez `msgctl`).

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

### 5. Mechanizmy synchronizacji i IPC

**- Pamięć dzielona (`shmget`/ `shmat`):**
Obiekt `StanSklepu` jest mapowany przez każdy proces. Zawiera:

- Tablice struktur `Kasa` (statusy, liczniki kolejek).
- Liczniki globalne (liczba klientów w sklepie, flagi ewakuacji).
- Magazyn produktów.

**- Semafory (`semop`):**
Zestaw semaforów kontroluje dostęp do zasobów, zapobiegając niesynchronizowanemu dostępowi do danych:

- `MUTEX_PAMIEC_WSPOLDZIELONA`: Chroni modyfikację stanu sklepu (blokuje dostęp do pamięci współdzielonej).
- `MUTEX_KOLEJKI_VIP`: Synchronizuje operacje podnoszenia limitu kolejki dla odpowiedzi VIP.
- `SEM_NOWY_KLIENT`: Sygnalizuje nowego klienta dla wątku skalującego kas samoobsługowych.

**- Kolejki komunikatów (`msgrcv`/`msgsnd`):**

Umożliwiają dwukierunkową wymianę danych między procesami:

- **Osobne kolejki dla kas stacjonarnych:** Kasjer 1 używa `ID_IPC_KASA_1`, kasjer 2 używa `ID_IPC_KASA_2`.

- **Informacja zwrotna:** Klient nasłuchuje na specyficznym typie wiadomości (`BASE + własne_ID`), co pozwala mu wyłowić z kolejki tylko swój paragon. W odpowiedzi zwrotnej, zamiast (zbędnej dla klienta) liczby produktów, w polu `liczba_produktow` przesyłane jest ID kasy, która go obsłużyła. Dzięki temu unika się powiększania struktury komunikatu.

- **Mechanizm VIP:** Funkcja `WyslijKomunikatVIP` tymczasowo podnosi limit kolejki, by zagwarantować miejsce na odpowiedź nawet przy pełnej kolejce.

- **Logowanie nieblokujące:** Procesy wysyłają logi do kolejki loggera z flagą `IPC_NOWAIT`, dzięki czemu symulacja nie zatrzymuje się w oczekiwaniu na zapis na dysk.

## 6. Kluczowe Pseudokody

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
    fork() -> exec("kasa_samoobslugowa")  // Hybryda: 1 manager + 5 workerów
    fork() -> exec("pracownik")

Pętla symulacji:
    WHILE czas_trwania < limit I brak_ewakuacji:
        aktualizuj_czas()

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
        alarm(T)  // Timeout tylko dla samoobsługi
        wyslij_msg(MSG_SAMOOBSLUGA, koszyk)
        czekaj_na_odpowiedz()
        IF timeout:
            przejdz_do_kasy_stacjonarnej()

    IF idzie_do_stacjonarnej:
        wybierz_kase_z_mniejsza_kolejka(kasa1, kasa2)
        wyslij_msg(MSG_KASA_X, koszyk)
        czekaj_na_odpowiedz(MSG_RES_X)

Koniec:
    odejmij_sie_z_licznika_sklepu()
    wyjdz()
```

### 3. kasjer.c

```
Inicjalizacja:
    msg_id = PobierzIdKolejki(ID_KASY == 0 ? KASA_1 : KASA_2)

Pętla główna:
    WHILE TRUE:
        msg = odbierz_blokujaco(msg_id)

        IF msg.timestamp przedawniony:
            continue

        ustaw_stan(ZAJETA)
        FOR produkt in koszyk:
            sleep(czas_skanowania)
        wyslij_VIP(odpowiedz)
        ustaw_stan(WOLNA)
```

### 4. kasa_samoobslugowa.c

```
Proces główny (kasa 0 + manager):
    uruchom_kasy_poczatkowe(1, 2)
    uruchom_watek_skalujacy()

    WHILE TRUE:
        obsluz_klienta(0)

Wątek skalujący:
    WHILE TRUE:
        reap_zombie_children()
        czekaj_na_semafor(SEM_NOWY_KLIENT)

        wymagane = oblicz_wymagana_liczbe_kas()

        IF aktywne < wymagane:
            fork() -> uruchom_kase()
        ELSE IF klientow < K*(aktywne-3):
            zamknij_kase(SIGTERM)
```

## 7. Wykonane testy

Poniżej przedstawiono zbiór testów weryfikujących poprawność działania mechanizmów IPC w symulacji.

### Test 1 – Brak deadlocków (tryb bez opóźnień)

- **Polecenie**: `./dyskont.out 100 1 80`
- **Opis**: Uruchomienie symulacji z wyłączonymi opóźnieniami (`usleep`), maksymalną liczbą klientów 100, czasem symulacji 80 sekund.
- **Testowane mechanizmy IPC**: Wszystkie (kolejki komunikatów, semafory, pamięć współdzielona).
- **Oczekiwany wynik**: System przetwarza setki klientów w ułamku sekundy bez zakleszczenia (deadlock). Wszystkie procesy kończą się poprawnie, a zasoby IPC są zwolnione po zakończeniu. Brak komunikatów o błędach `EDEADLK`, `EAGAIN` czy przekroczeniu limitu kolejek.

### Test 2 – Przepustowość kolejek komunikatów

- **Polecenie**: `./dyskont.out 60 1 30` (tryb bez sleepów, 60 klientów)
- **Opis**: Weryfikacja poprawności dwukierunkowej komunikacji przez kolejki komunikatów przy dużym obciążeniu.
- **Testowane mechanizmy IPC**: `msgsnd`, `msgrcv`, `msgctl` (statystyki).
- **Oczekiwany wynik**:
  1. Każdy klient wysyła komunikat do kolejki kasy (`MSG_TYPE_KASA_1`, `MSG_TYPE_KASA_2` lub `MSG_TYPE_SAMOOBSLUGA`).
  2. Kasjer/kasa odbiera komunikat i odsyła odpowiedź VIP (`MSG_RES_*_BASE + id_klienta`).
  3. Klient odbiera tylko swój paragon (filtrowanie po `mtype`).
  4. Weryfikacja przez `kierownik` (opcja 5) pokazuje prawidłowe statystyki kolejek (`msgctl`).

### Test 3 – Synchronizacja semaforów (sekcje krytyczne)

- **Polecenie**: `./dyskont.out 50 0 60` (z opóźnieniami, 50 klientów)
- **Opis**: Test poprawności ochrony sekcji krytycznych przez semafory przy współbieżnym dostępie do pamięci współdzielonej.
- **Testowane mechanizmy IPC**: `semop` (zajmij/zwolnij), `MUTEX_PAMIEC_WSPOLDZIELONA`, `MUTEX_KOLEJKI_VIP`.
- **Oczekiwany wynik**:
  1. Licznik `liczba_klientow_w_sklepie` nigdy nie przyjmuje wartości ujemnej ani nie przekracza maksimum.
  2. Stan kas (`KASA_WOLNA`, `KASA_ZAJETA`) jest spójny – brak sytuacji, gdy dwóch klientów jest obsługiwanych przez tę samą kasę.
  3. Odpowiedzi VIP są wysyłane bez błędu `EAGAIN` (mutex VIP zapewnia podniesienie limitu kolejki).
  4. Brak komunikatów `Blad zwalniania semafora` (`ERANGE`).

### Test 4 – Odporność na przepełnienie kolejek i mechanizm VIP

- **Polecenie**: `./dyskont.out 80 1 20` + ręczne zatrzymanie kasjerów (`kill -STOP`)
- **Opis**: Symulacja scenariusza, gdy klienci wysyłają komunikaty szybciej niż kasjerzy je odbierają (np. po zatrzymaniu procesu kasjera).
- **Testowane mechanizmy IPC**: `msgsnd`, `msgctl` z `IPC_SET`, `WyslijKomunikatVIP` (podnoszenie limitu).
- **Oczekiwany wynik**:
  1. Kolejka komunikatów osiąga limit (domyślnie 16KB).
  2. Mechanizm VIP (`MUTEX_KOLEJKI_VIP` + `msgctl` z `IPC_SET`) tymczasowo podnosi limit kolejki, pozwalając wysłać odpowiedź do klienta nawet przy pełnej kolejce.
  3. Wszystkie logi są wyświetlane (blokujące `msgsnd` gwarantuje brak utraty logów).
  4. Po wznowieniu kasjera (`kill -CONT`) system wraca do normalnego działania.

## 8. Linki do kodu (Wymagane funkcje systemowe)

Poniżej znajdują się odniesienia do kluczowych mechanizmów systemowych wykorzystanych w projekcie:

**a. Tworzenie i obsługa plików (`open`, `close`, `read`, `write`, `pipe`)**

- `open` (otwarcie pliku logów): [logi.c:37](https://github.com/MrKamkar/Dyskont/blob/main/Dyskont/logi.c#L37)
- `write` (zapis do logu): [logi.c:103](https://github.com/MrKamkar/Dyskont/blob/main/Dyskont/logi.c#L103)
- `close` (zamknięcie pliku): [logi.c:109](https://github.com/MrKamkar/Dyskont/blob/main/Dyskont/logi.c#L109)
- `read` (odczyt z potoku sterującego): [main.c:38](https://github.com/MrKamkar/Dyskont/blob/main/Dyskont/main.c#L38)

**b. Tworzenie procesów (`fork`, `exec`, `exit`, `wait`)**

- `fork` (tworzenie procesu kasjera): [main.c:262](https://github.com/MrKamkar/Dyskont/blob/main/Dyskont/main.c#L262)
- `exec` (`execl` - nadpisanie obrazu procesu): [main.c:277](https://github.com/MrKamkar/Dyskont/blob/main/Dyskont/main.c#L277)
- `exit` (zakończenie procesu potomnego): [main.c:304](https://github.com/MrKamkar/Dyskont/blob/main/Dyskont/main.c#L304)
- `wait` (`waitpid` - oczekiwanie na dzieci): [main.c:69](https://github.com/MrKamkar/Dyskont/blob/main/Dyskont/main.c#L69)

**c. Tworzenie i obsługa wątków (`pthread_create`, `pthread_exit`)**

- `pthread_create` (utworzenie wątku loggera): [logi.c:130](https://github.com/MrKamkar/Dyskont/blob/main/Dyskont/logi.c#L130)
- `pthread_create` (wątek skalujący): [kasa_samoobslugowa.c:339](https://github.com/MrKamkar/Dyskont/blob/main/Dyskont/kasa_samoobslugowa.c#L339)
- `pthread_exit` (zakończenie wątku): [logi.c:110](https://github.com/MrKamkar/Dyskont/blob/main/Dyskont/logi.c#L110)
- `pthread_sigmask` (blokowanie sygnałów w wątku): [logi.c:21](https://github.com/MrKamkar/Dyskont/blob/main/Dyskont/logi.c#L21)

**d. Obsługa sygnałów (`kill`, `signal`, `sigaction`)**

- `sigaction` (rejestracja handlera SIGCHLD): [main.c:221](https://github.com/MrKamkar/Dyskont/blob/main/Dyskont/main.c#L221)
- `signal` (prosta rejestracja handlerów): [main.c:223](https://github.com/MrKamkar/Dyskont/blob/main/Dyskont/main.c#L223)
- `kill` (wysłanie sygnału do grupy procesów): [kierownik.c:95](https://github.com/MrKamkar/Dyskont/blob/main/Dyskont/kierownik.c#L95)

**e. Synchronizacja procesów (Semafory Systemu V)**

- `semget` (utworzenie/pobranie zestawu semaforów): [semafory.c:30](https://github.com/MrKamkar/Dyskont/blob/main/Dyskont/semafory.c#L30)
- `semop` (operacja na semaforze - wait/signal): [semafory.c:14](https://github.com/MrKamkar/Dyskont/blob/main/Dyskont/semafory.c#L14)
- `semctl` (ustawienie wartości/usunięcie): [semafory.c:42](https://github.com/MrKamkar/Dyskont/blob/main/Dyskont/semafory.c#L42)
  **f. Segmenty pamięci dzielonej (`shmget`, `shmat`, `shmdt`)**

- `shmget` (alokacja segmentu pamięci): [pamiec_wspoldzielona.c:25](https://github.com/MrKamkar/Dyskont/blob/main/Dyskont/pamiec_wspoldzielona.c#L25)
- `shmat` (dołączenie segmentu do przestrzeni adresowej): [pamiec_wspoldzielona.c:34](https://github.com/MrKamkar/Dyskont/blob/main/Dyskont/pamiec_wspoldzielona.c#L34)
- `shmdt` (odłączenie segmentu): [pamiec_wspoldzielona.c:51](https://github.com/MrKamkar/Dyskont/blob/main/Dyskont/pamiec_wspoldzielona.c#L51)

**g. Kolejki komunikatów (`msgget`, `msgsnd`, `msgrcv`, `msgctl`)**

- `msgget` (utworzenie kolejki): [kolejki.c:26](https://github.com/MrKamkar/Dyskont/blob/main/Dyskont/kolejki.c#L26)
- `msgsnd` (wysłanie komunikatu): [kolejki.c:50](https://github.com/MrKamkar/Dyskont/blob/main/Dyskont/kolejki.c#L50)
- `msgrcv` (odebranie komunikatu): [kolejki.c:60](https://github.com/MrKamkar/Dyskont/blob/main/Dyskont/kolejki.c#L60)
- `msgctl` (statystyki kolejki): [kolejki.c:70](https://github.com/MrKamkar/Dyskont/blob/main/Dyskont/kolejki.c#L70)
