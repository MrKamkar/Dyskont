#ifndef PAMIEC_WSPOLDZIELONA_H
#define PAMIEC_WSPOLDZIELONA_H

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
    double waga; // w gramach (dla pojedynczej sztuki)
} Produkt;

// Maksymalne rozmiary
#define MAX_KOLEJKA_SAMO 100
#define MAX_KOLEJKA_STACJONARNA 50
#define MAX_PRODUKTOW 50

// Liczba kas
#define LICZBA_KAS_SAMO 6
#define LICZBA_KAS_STACJONARNYCH 2

// Parametry symulacji
#define MIN_KAS_SAMO_CZYNNYCH 3
#define KLIENCI_NA_KASE 5  // Parametr K z opisu
#define MAX_CZAS_OCZEKIWANIA 30 // T w sekundach

// Stany kas
typedef enum {
    KASA_ZAMKNIETA,
    KASA_WOLNA,
    KASA_ZAJETA,
    KASA_ZABLOKOWANA
} StanKasy;

// Struktura pojedynczej kasy samoobsługowej
typedef struct {
    StanKasy stan;
    int id_klienta;           // ID obsługiwanego klienta (-1 jeśli wolna)
    time_t czas_rozpoczecia;  // Czas rozpoczęcia obsługi
} KasaSamoobslugowa;

// Struktura kasy stacjonarnej
typedef struct {
    StanKasy stan;
    int id_klienta;                  // ID obsługiwanego klienta
    int liczba_w_kolejce;            // Liczba osób czekających
    int kolejka[MAX_KOLEJKA_STACJONARNA]; // ID klientów w kolejce
    time_t czas_ostatniej_obslugi;   // Dla mechanizmu auto-zamykania
} KasaStacjonarna;

// Produkt z stanem magazynowym
typedef struct {
    Produkt produkt;      // Dane produktu (nazwa, cena, kategoria, waga)
    int ilosc;            // Liczba sztuk w magazynie
} ProduktMagazyn;

// Główna struktura stanu sklepu w pamięci współdzielonej
typedef struct {
    // Kasy
    KasaSamoobslugowa kasy_samo[LICZBA_KAS_SAMO];
    KasaStacjonarna kasy_stacjonarne[LICZBA_KAS_STACJONARNYCH];
    
    // Kolejka do kas samoobsługowych (wspólna)
    int kolejka_samo[MAX_KOLEJKA_SAMO];
    int liczba_w_kolejce_samo;
    
    // Liczniki
    int liczba_klientow_w_sklepie;
    int liczba_czynnych_kas_samo;
    
    // Baza produktów sklepu
    ProduktMagazyn magazyn[MAX_PRODUKTOW];
    int liczba_produktow;
    
    // Flagi kontrolne
    int flaga_ewakuacji; // Sygnał 3 od kierownika
    
    // Czas symulacji
    time_t czas_startu;
} StanSklepu;

// Funkcje zarządzające pamięcią współdzieloną
StanSklepu* InicjalizujPamiecWspoldzielona(const char* sciezka);
StanSklepu* DolaczPamiecWspoldzielona(const char* sciezka);
void OdlaczPamiecWspoldzielona(StanSklepu* stan);
void UsunPamiecWspoldzielona(const char* sciezka);

// Funkcje pomocnicze
void WyczyscStanSklepu(StanSklepu* stan);

#endif
