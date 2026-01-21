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
    KAT_CHEMIA,
    KAT_KOSMETYKI,
    KAT_MROZONKI,
    KAT_ART_DOMOWE,
    KAT_EKO,
    KAT_INNE
} KategoriaProduktu;

//Zwraca nazwe kategorii produktu jako string
const char* NazwaKategorii(KategoriaProduktu kat);

//Struktura produktu
typedef struct {
    char nazwa[50];
    double cena;
    KategoriaProduktu kategoria;
    double waga; //W gramach
} Produkt;

//Maksymalne rozmiary kolejek
#define MAX_KOLEJKA_SAMOOBSLUGOWA 30
#define MAX_KOLEJKA_STACJONARNA 10
#define MAX_PRODUKTOW 50

//Liczba kas
#define LICZBA_KAS_SAMOOBSLUGOWYCH 6
#define LICZBA_KAS_STACJONARNYCH 2

//Sciezka do pliku dla ftok() wspolna dla wszystkich procesow
#define IPC_SCIEZKA "./dyskont.out"

//Parametry symulacji
#define MIN_KAS_SAMO_CZYNNYCH 3
#define KLIENCI_NA_KASE 5  //Parametr K z README.md
#define MAX_KLIENTOW_ROWNOCZESNIE_DOMYSLNIE 100
#define PRZERWA_MIEDZY_KLIENTAMI_MS 500
#define CZAS_OCZEKIWANIA_T 5  //Czas oczekiwania, po ktorym klient moze przejsc do kasy stacjonarnej

//Makro do symulacyjnych usleep, ktore mozna wylaczyc w trybie testu
#define SYMULACJA_USLEEP(stan, us) if ((stan)->tryb_testu == 0) usleep(us);

//Stany kas
typedef enum {
    KASA_ZAMKNIETA,
    KASA_WOLNA,
    KASA_ZAJETA,
    KASA_ZAMYKANA
} StanKasy;
 
//Polecenia kierownika
typedef enum {
    POLECENIE_BRAK = 0,
    POLECENIE_OTWORZ_KASE_2 = 1,
    POLECENIE_ZAMKNIJ_KASE = 2
} PolecenieKierownika;

//Struktura kasy
typedef struct {
    StanKasy stan;
    int id_klienta; //ID obslugiwanego klienta
    pid_t pid;      //PID procesu obsługującego kasę (dla kas samoobslugowych)
} Kasa;



//Glowna struktura stanu sklepu w pamieci wspoldzielonej
typedef struct {

    //Kasy
    Kasa kasy_samoobslugowe[LICZBA_KAS_SAMOOBSLUGOWYCH];
    Kasa kasy_stacjonarne[LICZBA_KAS_STACJONARNYCH];


    //unsigned int liczba_w_kolejce_samoobslugowej; //Liczba klientow w kolejce do kasy samoobslugowej

    
    //Liczniki
    unsigned int liczba_klientow_w_sklepie;
    unsigned int liczba_czynnych_kas_samoobslugowych;
    
    //Baza produktow sklepu
    Produkt magazyn[MAX_PRODUKTOW];
    unsigned int liczba_produktow; //Rozmiar tablicy magazyn
    
    //Flagi kontrolne
    PolecenieKierownika polecenie_kierownika; //Polecenie od kierownika
    int id_kasy_do_zamkniecia; //Ktora kasa ma byc zamknieta (0 lub 1)
    
    //PID glownego procesu do wysylania sygnalow
    pid_t pid_glowny;
    
    //Czas symulacji
    time_t czas_startu;
    
    //Tryb testu (0 to normalny, 1 to bez usleepow)
    int tryb_testu;
    
    //Maksymalna liczba klientow rownoczesnie w sklepie
    unsigned int max_klientow_rownoczesnie;
} StanSklepu;

//Funkcje zarzadzajace pamiecia wspoldzielona
StanSklepu* InicjalizujPamiecWspoldzielona();
StanSklepu* DolaczPamiecWspoldzielona();
void OdlaczPamiecWspoldzielona(StanSklepu* stan);
void UsunPamiecWspoldzielona();

//Funkcje pomocnicze
void WyczyscStanSklepu(StanSklepu* stan);

#endif
