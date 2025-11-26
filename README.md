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

# **Przykładowe testy**

### **Test 1 – Klient bez alkoholu**
**Opis:** Klient kupuje 3–10 produktów, bez alkoholu.  
**Oczekiwany wynik:** Szybka obsługa przy kasie samoobsługowej, brak weryfikacji wieku.

---

### **Test 2 – Klient kupuje alkohol**
**Opis:** Klient ma w koszyku produkt alkoholowy.  
**Oczekiwany wynik:** Wymagana **weryfikacja wieku** przez pracownika.

---

### **Test 3 – Zbyt długie oczekiwanie**
**Opis:** Klient czeka dłużej niż **T** w kolejce do kas samoobsługowych.  
**Oczekiwany wynik:** Klient przechodzi do **kasy stacjonarnej**, jeśli jest otwarta.

---

### **Test 4 – Awaria kasy samoobsługowej**
**Opis:** Kasa samoobsługowa ulega **blokadzie**.  
**Oczekiwany wynik:** Pracownik dokonuje **interwencji**, klient kończy transakcję.

