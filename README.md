# Temat 16 – **Dyskont**

**Kamil Karpiel**  
Grupa 1 | Studia stacjonarne | III semestr | Nr albumu 155184

W pewnym dyskoncie jest łącznie **8 kas**: **2 kasy stacjonarne** i **6 kas samoobsługowych**. Klienci
przychodzą do sklepu w dowolnych momentach czasu i przebywają w nim przez pewien określony
(losowy) dla każdego z nich czas robiąc zakupy – **od 3 do 10 produktów** różnych kategorii (owoce,
warzywa, pieczywo, nabiał, alkohol, wędliny, …). Większość klientów korzysta z kas
samoobsługowych (**ok. 95%**), pozostali (**ok. 5%**) stają w kolejce do kas stacjonarnych.

## **Zasady działania kas samoobsługowych:**

– Do wszystkich kas samoobsługowych jest **jedna kolejka**;  
– Klient podchodzi do **pierwszej wolnej kasy**;  
– Zawsze działają co najmniej **3 kasy samoobsługowe**;  
– Na każdych **K klientów** znajdujących się na terenie dyskontu powinno przypadać min. **1
czynne stanowisko kasowe**.  
– Jeśli liczba klientów jest mniejsza niż **K×(N-3)**, gdzie **N** oznacza liczbę czynnych kas, to **jedna
z kas zostaje zamknięta**;  
– Jeżeli czas oczekiwania w kolejce na kasę samoobsługową jest dłuższy niż **T**, klient może
przejść do **kasy stacjonarnej** jeżeli jest otwarta;  
– Przy zakupie produktów z alkoholem konieczna jest **weryfikacja kupującego** przez obsługę
(**wiek > 18**);  
– Klient kasuje produkty, płaci kartą i otrzymuje **wydruk (raport)** z listą zakupów i zapłaconą
kwotą;  
– Co pewien losowy czas kasa się **blokuje** (np. waga towaru nie zgadza się z wyborem klienta)
– wówczas konieczna jest **interwencja obsługi**, aby odblokować kasę.

## **Zasady działania kas stacjonarnych:**

– Generalnie kasy są **zamknięte**;  
– Jeżeli liczba osób stojących w kolejce do kasy jest większa niż **3**, otwierana jest **kasa 1**;  
– Jeżeli po obsłużeniu ostatniego klienta w kolejce przez **30 sekund** nie pojawi się następny
klient, kasa jest **zamykanа**;  
– **Kasa 2** jest otwierana jedynie na **bezpośrednie polecenie kierownika** – zasady zamykania jak
dla kasy 1;  
– Jeżeli kasa 2 jest otwarta, to każdej z kas tworzy się **osobna kolejka klientów** (klienci z kolejki
do kasy 1 mogą przejść do kolejki do kasy 2);  
– Jeśli w kolejce do kasy czekali klienci (przed ogłoszeniem decyzji o jej zamknięciu) to powinni
zostać **obsłużeni** przez tę kasę.

Na polecenie kierownika sklepu (**sygnał 1**) jest **otwierana kasa 2**. Na polecenie kierownika sklepu
(**sygnał 2**) jest **zamykana dana kasa** (1 lub 2). Na polecenie kierownika sklepu (**sygnał 3**) klienci
**natychmiast opuszczają supermarket** bez robienia zakupów, a następnie po wyjściu klientów
zamykane są **wszystkie kasy**.

Napisz program **kierownika**, **kasjera**, **kasy samoobsługowej**, **pracownika obsługi** i **klienta**. Raport z
przebiegu symulacji zapisać w pliku (plikach) tekstowym.

---

# **Wykonane testy**

Poniżej przedstawiono zbiór testów weryfikujących poprawność działania mechanizmów IPC w symulacji.

### Test 1 – Test Obciążeniowy (Brak Deadlocków)

- **Polecenie**: `./dyskont.out 10000 200 1`
- **Opis**: Weryfikacja stabilności systemu przy ekstremalnie szybkiej generacji i obsłudze procesów (tryb bez `usleepów`). Test weryfikuje odporność na deadlocki, poprawne czyszczenie procesów zombie oraz poprawność działania algorytmu skalowania kas (+/- 1 kasa na każde K klientów, minimum 3).
- **Oczekiwany wynik**:
  1. Kasa swoje kończy działanie automatycznie po obsłużeniu wszystkich 10 000 klientów i upłynięciu czasu bezczynności.
  2. Brak procesów zombie (wszystkie procesy klientów są poprawnie odebrane przez `wait` w procesie głównym generatora, co można potwierdzić brakiem logów o zombie).
  3. Weryfikacja logiki skalowania: Gdy liczba klientów w sklepie spadnie do 0, system dynamicznie przez wątek skalujący redukuje liczbę kas samoobsługowych do poziomu 3, zgodnie z wzorem $klientów < K \cdot (N-3)$.
  4. Brak komunikatów błędów IPC na standardowym wyjściu błędów.
  5. `kierownik` (opcja 5) pokazuje na koniec 0 klientów w sklepie oraz powrót do 3 czynnych kas samoobsługowych.

![Zrzut 1](img/test1_1.png)  
_Zrzut 1: Kasa kończy działanie po czasie bezczynności_

![Zrzut 2](img/test1_2.png)  
_Zrzut 2: Poprawne skalowanie kas (3 procesy) i brak procesów zombie_

![Zrzut 3](img/test1_3.png)  
_Zrzut 3: Kierownik pokazuje 0 klientów w sklepie oraz 3 czynne kasy samoobsługowe_

### Test 2 – Odporność na przepełnienie kolejek (VIP)

- **Polecenie**: `./dyskont.out 10000 1000 1`
- **Opis**: Test weryfikujący, czy system radzi sobie z lawinowym napływem komunikatów do kas samoobsługowych, gdzie kolejki mogą się przepełnić. Test sprawdza, czy mechanizm `WyslijKomunikatVIP` (zwiększanie limitu kolejki) pozwala zawsze odesłać odpowiedź.
- **Oczekiwany wynik**:
  1. Kasy samoobsługowe nie ulegają deadlockowi na operacji `msgsnd` mimo pełnej kolejki (blokujący `msgsnd` przechodzi, ponieważ komunikaty zwrotne VIP nie podlegają limitowaniu przez semafory).
  2. Wszystkie 10 000 klientów otrzymuje swoje paragony.
  3. Kierownik raportuje pełne kolejki (domyślnie 408 komunikatów [16384B / 40B - 1]), ale system nadal przetwarza klientów bez utraty danych.

![Zrzut 1](img/test2_1.png)  
_Zrzut 1: Prawie pełne obłożenie kolejek (~403 komunikatów) a system nadal działa_

![Zrzut 2](img/test2_2.png)  
_Zrzut 2: Program nie zostawia żadnych procesów zombie_

![Zrzut 3](img/test2_3.png)  
_Zrzut 3: Wszystkie 10 000 klientów dostało swoje paragony (brak utraty danych)_

### Test 3 – Ręczne zarządzanie kasami w szczycie (Signały)

- **Polecenie**: `./dyskont.out 10000 1000 0` + użycie `kierownik` (opcje 1-3)
- **Opis**: Weryfikacja stabilności systemu, gdy podczas obsługi 10 000 klientów. Kierownik dynamicznie otwiera i zamyka kasy stacjonarne. Sprawdza poprawność przekierowywania klientów przez wątek zarządcy kas w momencie nagłej zmiany stanu kasy.
- **Oczekiwany wynik**:
  1. Zamknięcie kasy (status `KASA_ZAMYKANA`) powoduje dokończenie obsługi wszystkich klientów z jej prywatnej kolejki, a następnie całkowite wyłączenie (`KASA_ZAMKNIETA`).
  2. Wątek Zarządcy natychmiast przestaje kierować nowych klientów do zamykanej kasy, przekierowując ich do drugiej kasy lub pozostawiając w kolejce wspólnej (gdzie oczekują np. na automatyczne otwarcie Kasy 1).
  3. Otwarcie kasy powoduje natychmiastowe przejęcie części ruchu i rozładowanie kolejki wspólnej tak, by klient wybrał najmniejszą kolejkę.
  4. System działa stabilnie – nie zawiesza się (deadlock) i nie kończy niespodziewanie błędem segmentacji (segfault) mimo intensywnych zmian konfiguracji kas.

![Zrzut 1](img/test3_1.png)  
_Zrzut 1: Stan początkowy - stabilna praca, Kasa 1 obsługuje klientów_

![Zrzut 2](img/test3_2.png)  
_Zrzut 2: Zamknięcie kas - Kasa 1 zamykana, klienci gromadzą się we wspólnej kolejce_

![Zrzut 3](img/test3_3.png)
_Zrzut 3: Interwencja - otwarcie Kasy 2 powoduje rozładowanie zatoru z kolejki samoobsługowej_
(Jest to zrzut z innego logu, gdyż zmieniłem sposób ich wyświetlania w trakcie poprawiania kodu)

### Test 4 – Ewakuacja przy maksymalnym obciążeniu

- **Polecenie**: `./dyskont.out 10000 1000 0` -> Wywołanie Ewakuacji (przez `kierownik`, `kill -SIGTERM` lub `CTRL + C`)
- **Opis**: Sprawdzenie czy system potrafi bezpiecznie i całkowicie posprzątać zasoby w momencie największego obciążenia (tysiące aktywnych procesów klientów i procesów obsługujących kasy) przy włączonej symulacji `usleepów`.
- **Oczekiwany wynik**:
  1. Główny proces natychmiast wysyła SIGTERM do grupy procesów.
  2. Wszystkie procesy (3 managery + tysiące klientów) kończą się w ciągu kilku sekund.
  3. Polecenie `ipcs` wykazuje brak wiszących kolejek, semaforów i pamięci współdzielonej.
  4. Brak procesów zombie (wszystkie poprawnie odebrane przez `wait`).

![Zrzut 1](img/test4_1.png)  
_Zrzut 1: Uruchomienie procedury ewakuacji (SIGTERM) przy pełnym obciążeniu sklepu_

![Zrzut 2](img/test4_2.png)  
_Zrzut 2: Potwierdzenie czystego środowiska (brak wiszących zasobów IPC) po zakończeniu programu_
