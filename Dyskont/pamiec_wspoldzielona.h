#ifndef PAMIEC_WSPOLDZIELONA_H
#define PAMIEC_WSPOLDZIELONA_H

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "semafory.h"
#include "logi.h"
#include <time.h>

//Identyfikatory projektow dla ftok()
#define ID_IPC_LOGI 'A'
#define ID_IPC_SEMAFORY 'M'
#define ID_IPC_PAMIEC 'S'

//Typy komunikatow
#define MSG_TYPE_KASA_1 1
#define MSG_TYPE_KASA_2 2
#define MSG_TYPE_KASA_WSPOLNA 3
#define MSG_TYPE_SAMOOBSLUGA 4
#define MSG_TYPE_PRACOWNIK 5

//Kanal odpowiedzi
#define MSG_RES_SAMOOBSLUGA_BASE 10000
#define MSG_RES_STACJONARNA_BASE 20000
#define MSG_RES_PRACOWNIK_BASE   30000

//Kody operacji dla pracownika
#define OP_WERYFIKACJA_WIEKU 1
#define OP_ODBLOKOWANIE_KASY 2

//Struktura komunikatu dla kasy stacjonarnej
typedef struct {
    long mtype;       //Typ komunikatu
    int id_klienta;   //ID klienta
    unsigned int liczba_produktow;
    double suma_koszyka;
    int ma_alkohol;
    unsigned int wiek;
} MsgKasaStacj;

//Struktura komunikatu dla kasy samoobslugowej
typedef struct {
    long mtype;
    int id_klienta;
    unsigned int liczba_produktow;
    double suma_koszyka;
    int ma_alkohol;
    unsigned int wiek;
    time_t timestamp; //Znacznik czasu wyslania
} MsgKasaSamo;

//Struktura komunikatu dla pracownika obslugi
typedef struct {
    long mtype;
    int id_kasy;      //ID kasy zglaszajacej
    int operacja;     //Typ zlecenia/wynik
    unsigned int wiek;
} MsgPracownik;

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
#define CZAS_OCZEKIWANIA_T 20  //Czas oczekiwania, po ktorym klient moze przejsc do kasy stacjonarnej

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

    
    //Liczniki
    unsigned int liczba_klientow_w_sklepie;
    unsigned int liczba_czynnych_kas_samoobslugowych;
    
    //Baza produktow sklepu
    Produkt magazyn[MAX_PRODUKTOW];
    unsigned int liczba_produktow; //Rozmiar tablicy magazyn
    
    //Flagi kontrolne
    PolecenieKierownika polecenie_kierownika; 
    int id_kasy_do_zamkniecia; //Ktora kasa ma byc zamknieta (0 lub 1)
    
    //PID glownego procesu do wysylania sygnalow
    pid_t pid_glowny;
    pid_t pid_kierownika; //PID procesu kierownika (panelu sterowania)
    
    //Czas symulacji
    time_t czas_startu;
    
    //Tryb testu (0 to normalny, 1 to bez usleepow)
    int tryb_testu;
    
    //Maksymalna liczba klientow rownoczesnie w sklepie
    unsigned int max_klientow_rownoczesnie;
    
    //Tablica pomijanych klientow
    int pomijani_klienci[];
} StanSklepu;

//Oblicza rozmiar segmentu pamieci wspoldzielonej dla danej liczby klientow
#define ROZMIAR_PAMIECI_WSPOLDZIELONEJ(max_klientow) \
    (sizeof(StanSklepu) + sizeof(int) * (max_klientow))

//Funkcje zarzadzajace pamiecia wspoldzielona
StanSklepu* InicjalizujPamiecWspoldzielona(unsigned int max_klientow);
StanSklepu* DolaczPamiecWspoldzielona();
void OdlaczPamiecWspoldzielona(StanSklepu* stan);
void UsunPamiecWspoldzielona();

//Funkcje pomocnicze
void WyczyscStanSklepu(StanSklepu* stan);
key_t GenerujKluczIPC(char id_projektu);
int InicjalizujProcesPochodny(StanSklepu** stan, int* sem_id, const char* nazwa_procesu);

//Zarzadzanie tablica pomijanych klientow (wycofanie z kolejki samoobslugowej)
int DodajPomijanego(StanSklepu* stan, int sem_id, int id_klienta);
int CzyPominiety(StanSklepu* stan, int sem_id, int id_klienta);

//Aliasy dla kasjera (z ujemnym ID)
int DodajZmigrowanego(StanSklepu* stan, int sem_id, int id_klienta);
int CzyZmigrowany(StanSklepu* stan, int sem_id, int id_klienta);

#endif
