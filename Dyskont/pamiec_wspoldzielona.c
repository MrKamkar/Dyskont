#include "wspolne.h"

//Zwraca nazwe kategorii produktu
const char* NazwaKategorii(KategoriaProduktu kat) {
    switch (kat) {
        case KAT_OWOCE:    return "Owoce";
        case KAT_WARZYWA:  return "Warzywa";
        case KAT_PIECZYWO: return "Pieczywo";
        case KAT_NABIAL:   return "Nabial";
        case KAT_ALKOHOL:  return "Alkohol";
        case KAT_WEDLINY:  return "Wedliny";
        case KAT_NAPOJE:   return "Napoje";
        case KAT_SLODYCZE: return "Slodycze";
        case KAT_INNE:     return "Inne";
        default:           return "Nieznana";
    }
}


//Funkcja do pobierania ID segmentu pamieci wspoldzielonej
static int PobierzIdSegmentu(int flagi) {
    key_t klucz = GenerujKluczIPC(ID_IPC_PAMIEC);
    if (klucz == -1) return -1;
    
    int shm_id = shmget(klucz, sizeof(StanSklepu), flagi);
    if (shm_id == -1) if (errno != ENOENT) perror("Blad shmget");
    return shm_id;
}

//Dolaczenie do segmentu pamieci wspoldzielonej
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
static const Produkt DANE_PRODUKTOW[] = {
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

//Czyszczenie struktury stanu
void WyczyscStanSklepu(StanSklepu* stan) {
    if (!stan) return;

    //Wyzerowanie calej struktury
    memset(stan, 0, sizeof(StanSklepu));

    //Inicjalizacja kas samoobslugowych
    for (int i = 0; i < LICZBA_KAS_SAMOOBSLUGOWYCH; i++) {
        if (i < MIN_KAS_SAMO_CZYNNYCH) stan->kasy_samo[i].stan = KASA_WOLNA;
        else stan->kasy_samo[i].stan = KASA_ZAMKNIETA;
        stan->kasy_samo[i].id_klienta = -1;
    }

    //Inicjalizacja kas stacjonarnych
    for (int i = 0; i < LICZBA_KAS_STACJONARNYCH; i++) {
        stan->kasy_stacjonarne[i].stan = KASA_ZAMKNIETA;
        stan->kasy_stacjonarne[i].id_klienta = -1;
    }

    //Inicjalizacja licznikow
    stan->liczba_klientow_w_sklepie = 0;
    stan->liczba_czynnych_kas_samoobslugowych = MIN_KAS_SAMO_CZYNNYCH;
    stan->polecenie_kierownika = POLECENIE_BRAK;
    stan->id_kasy_do_zamkniecia = -1;

    //Inicjalizacja bazy produktow
    stan->liczba_produktow = sizeof(DANE_PRODUKTOW) / sizeof(DANE_PRODUKTOW[0]);
    for (unsigned int i = 0; i < stan->liczba_produktow; i++) {
        stan->magazyn[i] = DANE_PRODUKTOW[i];
    }

    //Zapisanie czasu startu
    stan->czas_startu = time(NULL);
    
    //Ustawienie domyslnej wartosci max klientow rownoczesnie
    stan->max_klientow_rownoczesnie = MAX_KLIENTOW_ROWNOCZESNIE_DOMYSLNIE;
}
