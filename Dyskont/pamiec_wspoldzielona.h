#ifndef PAMIEC_WSPOLDZIELONA_H
#define PAMIEC_WSPOLDZIELONA_H

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

//Kategorie produktow
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

//Struktura produktu
typedef struct {
    char nazwa[50];
    double cena;
    KategoriaProduktu kategoria;
    double waga; //w gramach (dla pojedynczej sztuki)
} Produkt;

//Maksymalne rozmiary kolejek
#define MAX_KOLEJKA_SAMO 500
#define MAX_KOLEJKA_STACJONARNA 200
#define MAX_PRODUKTOW 50

//Liczba kas
#define LICZBA_KAS_SAMOOBSLUGOWYCH 6
#define LICZBA_KAS_STACJONARNYCH 2

//Sciezka do pliku dla ftok() - wspolna dla wszystkich procesow
#define IPC_SCIEZKA "./dyskont.out"

//Parametry symulacji
#define MIN_KAS_SAMO_CZYNNYCH 3
#define KLIENCI_NA_KASE 5  //Parametr K z opisu
#define MAX_CZAS_OCZEKIWANIA 30 //T w sekundach
#define MAX_KLIENTOW_ROWNOCZESNIE_DOMYSLNIE 1000  //Domyslna wartosc
#define PRZERWA_MIEDZY_KLIENTAMI_MS 50

//Makro do symulacyjnych usleep - pomija sleep gdy tryb_testu == 1
#define SYMULACJA_USLEEP(stan, us) do { if ((stan)->tryb_testu == 0) usleep(us); } while(0)

//Stany kas
typedef enum {
    KASA_ZAMKNIETA,
    KASA_WOLNA,
    KASA_ZAJETA,
    KASA_ZABLOKOWANA,
    KASA_ZAMYKANA
} StanKasy;

//Struktura pojedynczej kasy samoobslugowej
typedef struct {
    StanKasy stan;
    int id_klienta;           //ID obslugiwanego klienta (-1 jesli wolna)
    time_t czas_rozpoczecia;  //Czas rozpoczecia obslugi
} KasaSamoobslugowa;

//Struktura kasy stacjonarnej
typedef struct {
    StanKasy stan;
    int id_klienta;                  //ID obslugiwanego klienta
    int liczba_w_kolejce;            //Liczba osob czekajacych
    int kolejka[MAX_KOLEJKA_STACJONARNA]; //ID klientow w kolejce
    time_t czas_ostatniej_obslugi;   //Dla mechanizmu auto-zamykania
} KasaStacjonarna;

//Produkt z stanem magazynowym
typedef struct {
    Produkt produkt;      //Dane produktu (nazwa, cena, kategoria, waga)
    int ilosc;            //Liczba sztuk w magazynie
} ProduktMagazyn;

//Glowna struktura stanu sklepu w pamieci wspoldzielonej
typedef struct {
    //Kasy
    KasaSamoobslugowa kasy_samo[LICZBA_KAS_SAMOOBSLUGOWYCH];
    KasaStacjonarna kasy_stacjonarne[LICZBA_KAS_STACJONARNYCH];
    
    //Kolejka do kas samoobslugowych (wspolna)
    int kolejka_samo[MAX_KOLEJKA_SAMO];
    int liczba_w_kolejce_samo;
    
    //Liczniki
    int liczba_klientow_w_sklepie;
    int liczba_czynnych_kas_samo;
    
    //Baza produktow sklepu
    ProduktMagazyn magazyn[MAX_PRODUKTOW];
    int liczba_produktow;
    
    //Flagi kontrolne
    int flaga_ewakuacji; //Sygnal 3 od kierownika
    int polecenie_kierownika;    //Polecenie od kierownika (0=brak, 1=otworz, 2=zamknij, 3=ewakuacja)
    int id_kasy_do_zamkniecia;   //Ktora kasa ma byc zamknieta (0 lub 1)
    
    //PID glownego procesu (do wysylania sygnalow)
    pid_t pid_glowny;
    
    //Czas symulacji
    time_t czas_startu;
    
    //Tryb testu (0=normalny, 1=bez sleepow symulacyjnych)
    int tryb_testu;
    
    //Maksymalna liczba klientow rownoczesnie w sklepie
    int max_klientow_rownoczesnie;
} StanSklepu;

//Funkcje zarzadzajace pamiecia wspoldzielona
StanSklepu* InicjalizujPamiecWspoldzielona();
StanSklepu* DolaczPamiecWspoldzielona();
void OdlaczPamiecWspoldzielona(StanSklepu* stan);
void UsunPamiecWspoldzielona();

//Funkcje pomocnicze
void WyczyscStanSklepu(StanSklepu* stan);

#endif
