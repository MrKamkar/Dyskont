#include "pamiec_wspoldzielona.h"
#include <errno.h>

//Generowanie klucza dla pamieci wspoldzielonej
static key_t GenerujKlucz() {
    key_t klucz = ftok(IPC_SCIEZKA, 'S');
    if (klucz == -1) {
        perror("Blad generowania klucza pamieci wspoldzielonej");
    }
    return klucz;
}

//Funkcja pomocnicza: pobranie ID segmentu
static int PobierzIdSegmentu(int flagi) {
    key_t klucz = GenerujKlucz();
    if (klucz == -1) return -1;
    
    int shm_id = shmget(klucz, sizeof(StanSklepu), flagi);
    if (shm_id == -1) {
        perror("Blad shmget");
    }
    return shm_id;
}

//Dolaczenie do segmentu
static StanSklepu* DolaczDoSegmentu(int shm_id) {
    StanSklepu* stan = (StanSklepu*)shmat(shm_id, NULL, 0);
    if (stan == (void*)-1) {
        perror("Blad dolaczenia pamieci dzielonej");
        return NULL;
    }
    return stan;
}

//Inicjalizacja pamieci wspoldzielonej
StanSklepu* InicjalizujPamiecWspoldzielona() {
    int shm_id = PobierzIdSegmentu(IPC_CREAT | 0600);
    if (shm_id == -1) exit(1);

    StanSklepu* stan = DolaczDoSegmentu(shm_id);
    if (!stan) exit(1);

    WyczyscStanSklepu(stan);
    return stan;
}

//Dolaczenie do istniejacej pamieci wspoldzielonej
StanSklepu* DolaczPamiecWspoldzielona() {
    int shm_id = PobierzIdSegmentu(0600);
    if (shm_id == -1) exit(1);

    StanSklepu* stan = DolaczDoSegmentu(shm_id);
    if (!stan) exit(1);

    return stan;
}

//Odlaczenie od pamieci wspoldzielonej
void OdlaczPamiecWspoldzielona(StanSklepu* stan) {
    if (stan && shmdt(stan) == -1) {
        perror("Blad odlaczenia pamieci dzielonej");
    }
}

//Usuniecie pamieci wspoldzielonej
void UsunPamiecWspoldzielona() {
    int shm_id = PobierzIdSegmentu(0600);
    if (shm_id == -1) return;

    if (shmctl(shm_id, IPC_RMID, NULL) == -1) {
        perror("Blad usuniecia segmentu pamieci");
    }
}

//Statyczne dane inicjalizacyjne dla produktow
static const struct {
    Produkt dane;
    int ilosc_poczatkowa;
} DANE_PRODUKTOW[] = {
    { {"Chleb", 3.50, KAT_PIECZYWO, 500.0}, 20 },
    { {"Bulka", 0.80, KAT_PIECZYWO, 60.0}, 100 },
    { {"Mleko", 4.20, KAT_NABIAL, 1000.0}, 50 },
    { {"Jogurt", 2.10, KAT_NABIAL, 150.0}, 60 },
    { {"Jajka (10szt)", 12.50, KAT_NABIAL, 600.0}, 30 },
    { {"Jablka", 3.00, KAT_OWOCE, 1000.0}, 80 },
    { {"Banany", 4.50, KAT_OWOCE, 1000.0}, 60 },
    { {"Ziemniaki", 2.00, KAT_WARZYWA, 1000.0}, 150 },
    { {"Pomidory", 8.00, KAT_WARZYWA, 500.0}, 40 },
    { {"Szynka", 45.00, KAT_WEDLINY, 500.0}, 25 },
    { {"Kielbasa", 25.00, KAT_WEDLINY, 400.0}, 35 },
    { {"Woda 1.5L", 2.00, KAT_NAPOJE, 1500.0}, 100 },
    { {"Cola", 6.00, KAT_NAPOJE, 1000.0}, 70 },
    { {"Guma do zucia", 3.00, KAT_SLODYCZE, 20.0}, 200 },
    { {"Czekolada", 5.00, KAT_SLODYCZE, 100.0}, 80 },
    { {"Chipsy", 6.50, KAT_SLODYCZE, 150.0}, 50 },
    { {"Piwo Jasne", 4.50, KAT_ALKOHOL, 500.0}, 120 },
    { {"Wino Czerwone", 25.00, KAT_ALKOHOL, 750.0}, 40 },
    { {"Wodka 0.5L", 35.00, KAT_ALKOHOL, 900.0}, 30 }
};

//Czyszczenie struktury stanu
void WyczyscStanSklepu(StanSklepu* stan) {
    if (!stan) return;

    //Wyzerowanie calej struktury
    memset(stan, 0, sizeof(StanSklepu));

    //Inicjalizacja kas samoobslugowych
    for (int i = 0; i < LICZBA_KAS_SAMOOBSLUGOWYCH; i++) {
        stan->kasy_samo[i].stan = KASA_WOLNA;
        stan->kasy_samo[i].id_klienta = -1;
        stan->kasy_samo[i].czas_rozpoczecia = 0;
    }

    //Inicjalizacja kas stacjonarnych
    for (int i = 0; i < LICZBA_KAS_STACJONARNYCH; i++) {
        stan->kasy_stacjonarne[i].stan = KASA_ZAMKNIETA;
        stan->kasy_stacjonarne[i].id_klienta = -1;
        stan->kasy_stacjonarne[i].liczba_w_kolejce = 0;
        stan->kasy_stacjonarne[i].czas_ostatniej_obslugi = 0;
    }

    //Inicjalizacja kolejki samoobslugowej
    stan->liczba_w_kolejce_samo = 0;
    for (int i = 0; i < MAX_KOLEJKA_SAMO; i++) {
        stan->kolejka_samo[i] = -1;
    }

    //Inicjalizacja licznikow
    stan->liczba_klientow_w_sklepie = 0;
    stan->liczba_czynnych_kas_samo = MIN_KAS_SAMO_CZYNNYCH;
    stan->flaga_ewakuacji = 0;
    stan->polecenie_kierownika = 0;
    stan->id_kasy_do_zamkniecia = -1;

    //Inicjalizacja bazy produktow
    stan->liczba_produktow = sizeof(DANE_PRODUKTOW) / sizeof(DANE_PRODUKTOW[0]);
    for (int i = 0; i < stan->liczba_produktow; i++) {
        stan->magazyn[i].produkt = DANE_PRODUKTOW[i].dane;
        stan->magazyn[i].ilosc = DANE_PRODUKTOW[i].ilosc_poczatkowa;
    }

    //Zapisanie czasu startu
    stan->czas_startu = time(NULL);
}
