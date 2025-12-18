#include "klient.h"

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

void DodajLosowyProdukt(Klient* k, const StanSklepu* stan_sklepu) {
    if (!k || !stan_sklepu || k->liczba_produktow >= k->ilosc_planowana) return;
    
    int indeks = rand() % stan_sklepu->liczba_produktow;
    k->koszyk[k->liczba_produktow++] = stan_sklepu->magazyn[indeks].produkt;
}

void ZrobZakupy(Klient* k, const StanSklepu* stan_sklepu) {
    if (!k || !k->koszyk || !stan_sklepu) return;

    // Po prostu wypełniamy koszyk do zaplanowanej ilości
    while (k->liczba_produktow < k->ilosc_planowana) {
        
        // Symulacja namysłu
        int czas_namyslu_ms = 100 + (rand() % 401);
        usleep(czas_namyslu_ms * 1000);  // od 100 do 500 ms

        // Wybór produktu (różnorodność)
        int indeks = 0;
        for (int proby = 0; proby < 20; proby++) {
            indeks = rand() % stan_sklepu->liczba_produktow;
            if (!CzyMaKategorie(k, stan_sklepu->magazyn[indeks].produkt.kategoria)) {
                break;
            }
        }
        
        k->koszyk[k->liczba_produktow++] = stan_sklepu->magazyn[indeks].produkt;
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
