#include "klient.h"

// Baza przykładowych produktów
static const Produkt BAZA_PRODUKTOW[] = {
    {"Chleb", 3.50, KAT_PIECZYWO, 500.0},
    {"Bulka", 0.80, KAT_PIECZYWO, 60.0},
    {"Mleko", 4.20, KAT_NABIAL, 1000.0},
    {"Jogurt", 2.10, KAT_NABIAL, 150.0},
    {"Jajka (10szt)", 12.50, KAT_NABIAL, 600.0},
    {"Jablka", 3.00, KAT_OWOCE, 1000.0},
    {"Banany", 4.50, KAT_OWOCE, 1000.0},
    {"Ziemniaki", 2.00, KAT_WARZYWA, 1000.0},
    {"Pomidory", 8.00, KAT_WARZYWA, 500.0},
    {"Szynka", 45.00, KAT_WEDLINY, 500.0},
    {"Kielbasa", 25.00, KAT_WEDLINY, 400.0},
    {"Woda 1.5L", 2.00, KAT_NAPOJE, 1500.0},
    {"Cola", 6.00, KAT_NAPOJE, 1000.0},
    {"Guma do zucia", 3.00, KAT_SLODYCZE, 20.0},
    {"Czekolada", 5.00, KAT_SLODYCZE, 100.0},
    {"Chipsy", 6.50, KAT_SLODYCZE, 150.0},
    {"Piwo Jasne", 4.50, KAT_ALKOHOL, 500.0},
    {"Wino Czerwone", 25.00, KAT_ALKOHOL, 750.0},
    {"Wodka 0.5L", 35.00, KAT_ALKOHOL, 900.0}
};
static const int LICZBA_PRODUKTOW_W_BAZIE = sizeof(BAZA_PRODUKTOW) / sizeof(Produkt);

// Sprawdzenie czy kategoria istnieje w koszyku
static int CzyMaKategorie(const Klient* k, KategoriaProduktu kat) {
    if (!k) return 0;
    for (int i = 0; i < k->liczba_produktow; i++) {
        if (k->koszyk[i].kategoria == kat) {
            return 1;
        }
    }
    return 0;
}

Klient* StworzKlienta(int id) {
    Klient* k = (Klient*)malloc(sizeof(Klient));
    if (!k) return NULL;

    k->id = id;
    k->wiek = 6 + (rand() % 85); // od 6 do 90 lat
    
    // Losujemy od razu ile kupi (3-10)
    k->ilosc_planowana = 3 + (rand() % 8);
    k->koszyk = (Produkt*)malloc(sizeof(Produkt) * k->ilosc_planowana);
    
    // Obsługa błędu alokacji
    if (!k->koszyk) {
        free(k);
        return NULL;
    }

    // Na start koszyk jest pusty (fizycznie), ale miejsce zaklepane
    k->liczba_produktow = 0;
    
    k->czas_dolaczenia_do_kolejki = 0;
    k->stan = STAN_ZAKUPY;

    return k;
}

void UsunKlienta(Klient* k) {
    if (k) {
        if (k->koszyk) {
            free(k->koszyk);
        }
        free(k);
    }
}

void DodajLosowyProdukt(Klient* k) {
    if (!k || k->liczba_produktow >= k->ilosc_planowana) return;
    
    int indeks = rand() % LICZBA_PRODUKTOW_W_BAZIE;
    k->koszyk[k->liczba_produktow++] = BAZA_PRODUKTOW[indeks];
}

void ZrobZakupy(Klient* k) {
    if (!k || !k->koszyk) return;

    // Po prostu wypełniamy koszyk do zaplanowanej ilości
    while (k->liczba_produktow < k->ilosc_planowana) {
        
        // Symulacja namysłu
        int czas_namyslu_ms = 100 + (rand() % 401);
        usleep(czas_namyslu_ms * 1000);  // od 100 do 500 ms

        // Wybór produktu (różnorodność)
        int indeks = 0;
        for (int proby = 0; proby < 20; proby++) {
            indeks = rand() % LICZBA_PRODUKTOW_W_BAZIE;
            if (!CzyMaKategorie(k, BAZA_PRODUKTOW[indeks].kategoria)) {
                break;
            }
        }
        
        k->koszyk[k->liczba_produktow++] = BAZA_PRODUKTOW[indeks];
    }
}

int CzyZawieraAlkohol(const Klient* k) {
    if (!k) return 0;
    for (int i = 0; i < k->liczba_produktow; i++) {
        if (k->koszyk[i].kategoria == KAT_ALKOHOL) {
            return 1;
        }
    }
    return 0;
}

double ObliczSumeKoszyka(const Klient* k) {
    if (!k) return 0.0;
    double suma = 0.0;
    for (int i = 0; i < k->liczba_produktow; i++) {
        suma += k->koszyk[i].cena;
    }
    return suma;
}
