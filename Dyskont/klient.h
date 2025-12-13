#ifndef KLIENT_H
#define KLIENT_H

#include <time.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

// Kategorie produktów
typedef enum {
    KAT_OWOCE,
    KAT_WARZYWA,
    KAT_PIECZYWO,
    KAT_NABIAL,
    KAT_ALKOHOL,
    KAT_WEDLINY,
    KAT_NAPOJE,
    KAT_SLODYCZE,
    KAT_INNE
} KategoriaProduktu;

// Struktura produktu
typedef struct {
    char nazwa[50];
    double cena;
    KategoriaProduktu kategoria;
    double waga; // w gramach
} Produkt;

// Stany, w jakich może znajdować się klient
typedef enum {
    STAN_ZAKUPY,
    STAN_KOLEJKA_SAMOOBSLUGOWA,
    STAN_KOLEJKA_KASA_1,
    STAN_KOLEJKA_KASA_2,
    STAN_PRZY_KASIE,
    STAN_WYJSCIE
} StanKlienta;

// Główna struktura klienta
typedef struct {
    int id;
    int wiek;
    
    // Koszyk (tabela o stałym rozmiarze ustalonym przy wejściu)
    Produkt* koszyk;
    int liczba_produktow;  // Aktualnie w koszyku
    int ilosc_planowana;   // Ile zamierza kupić (rozmiar bufora)
    
    // Czas
    time_t czas_dolaczenia_do_kolejki;
    
    StanKlienta stan;
} Klient;

// Funkcje zarządzające cyklem życia klienta
Klient* StworzKlienta(int id);
void UsunKlienta(Klient* k);

// Funkcje operacyjne
void DodajLosowyProdukt(Klient* k);
void ZrobZakupy(Klient* k);
int CzyZawieraAlkohol(const Klient* k);
double ObliczSumeKoszyka(const Klient* k);

#endif