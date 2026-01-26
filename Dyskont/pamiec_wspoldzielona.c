#include "pamiec_wspoldzielona.h"
#include <sys/msg.h>

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
        case KAT_CHEMIA:   return "Chemia";
        case KAT_KOSMETYKI:return "Kosmetyki";
        case KAT_MROZONKI: return "Mrozonki";
        case KAT_ART_DOMOWE: return "Art. Domowe";
        case KAT_EKO:      return "Eko";
        case KAT_INNE:     return "Inne";
        default:           return "Nieznana";
    }
}


//Funkcja do pobierania ID segmentu pamieci wspoldzielonej
static int PobierzIdSegmentu(size_t rozmiar, int flagi) {
    key_t klucz = GenerujKluczIPC(ID_IPC_PAMIEC);
    if (klucz == -1) return -1;
    
    int shm_id = shmget(klucz, rozmiar, flagi);
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
StanSklepu* InicjalizujPamiecWspoldzielona(unsigned int max_klientow) {
    size_t rozmiar = ROZMIAR_PAMIECI_WSPOLDZIELONEJ(max_klientow);
    int shm_id = PobierzIdSegmentu(rozmiar, IPC_CREAT | 0600);
    if (shm_id == -1) exit(1);

    StanSklepu* stan = DolaczDoSegmentu(shm_id);
    if (!stan) exit(1);

    //Ustawienie max_klientow PRZED czyszczeniem
    stan->max_klientow_rownoczesnie = max_klientow;
    WyczyscStanSklepu(stan);
    return stan;
}

//Dolaczenie do istniejacej pamieci wspoldzielonej
StanSklepu* DolaczPamiecWspoldzielona() {
    //Najpierw próbujemy z minimalnym rozmiarem by odczytac max_klientow
    int shm_id = PobierzIdSegmentu(sizeof(StanSklepu), 0600);
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
    int shm_id = PobierzIdSegmentu(0, 0600);
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
    {"Wodka 0.5L", 35.00, KAT_ALKOHOL, 900.0},
    {"Ply do naczyn", 8.50, KAT_CHEMIA, 1000.0},
    {"Proszek do prania", 25.00, KAT_CHEMIA, 2000.0},
    {"Szampon", 12.00, KAT_KOSMETYKI, 400.0},
    {"Mydlo", 3.50, KAT_KOSMETYKI, 100.0},
    {"Pizza mrozona", 14.00, KAT_MROZONKI, 400.0},
    {"Lody", 18.00, KAT_MROZONKI, 500.0},
    {"Papier toaletowy", 12.00, KAT_ART_DOMOWE, 800.0},
    {"Reczniki papierowe", 8.00, KAT_ART_DOMOWE, 600.0},
    {"Bio Jogurt", 4.50, KAT_EKO, 150.0},
    {"Napoj sojowy", 9.00, KAT_EKO, 1000.0}
};

//Czyszczenie struktury stanu
void WyczyscStanSklepu(StanSklepu* stan) {
    if (!stan) return;

    //Zachowaj max_klientow - bedzie potrzebne pozniej
    unsigned int max_klientow = stan->max_klientow_rownoczesnie;

    //Wyzerowanie calej struktury
    memset(stan, 0, sizeof(StanSklepu));
    
    //Przywroc max_klientow
    stan->max_klientow_rownoczesnie = max_klientow;

    //Inicjalizacja kas samoobslugowych
    for (int i = 0; i < LICZBA_KAS_SAMOOBSLUGOWYCH; i++) {
        stan->kasy_samoobslugowe[i].stan = KASA_ZAMKNIETA;
        stan->kasy_samoobslugowe[i].id_klienta = -1;
    }

    //Inicjalizacja kas stacjonarnych
    for (int i = 0; i < LICZBA_KAS_STACJONARNYCH; i++) {
        stan->kasy_stacjonarne[i].stan = KASA_ZAMKNIETA;
        stan->kasy_stacjonarne[i].id_klienta = -1;
    }

    //Inicjalizacja licznikow
    stan->liczba_klientow_w_sklepie = 0;
    stan->liczba_czynnych_kas_samoobslugowych = 0;
    stan->polecenie_kierownika = POLECENIE_BRAK;
    stan->id_kasy_do_zamkniecia = -1;

    //Inicjalizacja bazy produktow
    stan->liczba_produktow = sizeof(DANE_PRODUKTOW) / sizeof(DANE_PRODUKTOW[0]);
    for (unsigned int i = 0; i < stan->liczba_produktow; i++) {
        stan->magazyn[i] = DANE_PRODUKTOW[i];
    }

    //Zapisanie czasu startu
    stan->czas_startu = time(NULL);
    
    //Inicjalizacja tablicy pomijanych klientow (max_klientow_rownoczesnie juz ustawione)
    //0 = wolne miejsce, negatywne = samoobslugowa, pozytywne = migracja kasa1->kasa2
    for (unsigned int i = 0; i < stan->max_klientow_rownoczesnie; i++) {
        stan->pomijani_klienci[i] = 0;
    }
}

//Implementacja funkcji generujacej klucz
key_t GenerujKluczIPC(char id_projektu) {
    key_t klucz = ftok(IPC_SCIEZKA, id_projektu);
    if (klucz == -1) {
        char buf[64];
        sprintf(buf, "Blad generowania klucza IPC (id: %c)", id_projektu);
        perror(buf);
    }
    return klucz;
}


//Inicjalizacja procesu pochodnego
int InicjalizujProcesPochodny(StanSklepu** stan, int* sem_id, const char* nazwa_procesu) {
    
    //Dolaczenie do pamieci wspoldzielonej
    *stan = DolaczPamiecWspoldzielona();
    if (!*stan) {
        fprintf(stderr, "%s: Nie mozna dolaczyc do pamieci wspoldzielonej\n", nazwa_procesu);
        return -1;
    }
    
    //Dolaczenie do semaforow
    *sem_id = DolaczSemafory();
    if (*sem_id == -1) {
        fprintf(stderr, "%s: Nie mozna dolaczyc do semaforow\n", nazwa_procesu);
        OdlaczPamiecWspoldzielona(*stan);
        *stan = NULL;
        return -1;
    }
    
    //Inicjalizacja systemu logowania
    InicjalizujSystemLogowania();
    
    return 0;
}

//Dodaje ID do tablicy pomijanych (znajduje wolne miejsce)
int DodajPomijanego(StanSklepu* stan, int id_klienta) {
    if (!stan) return -1;

    for (unsigned int i = 0; i < stan->max_klientow_rownoczesnie; i++) {
        if (stan->pomijani_klienci[i] == 0) {
            stan->pomijani_klienci[i] = id_klienta;
            return 0; //Sukces
        }
    }
    return -1; //Brak miejsca
}

//Sprawdza czy ID jest w tablicy i usuwa je
int CzyPominiety(StanSklepu* stan, int id_klienta) {
    if (!stan) return 0;

    for (unsigned int i = 0; i < stan->max_klientow_rownoczesnie; i++) {
        if (stan->pomijani_klienci[i] == id_klienta) {
            stan->pomijani_klienci[i] = 0; //Usuwa z tablicy
            return 1; //Znaleziono i usunieto
        }
    }
    return 0; //Nie znaleziono
}

//Aliasy dla kasjera (z ujemnym ID)
int DodajZmigrowanego(StanSklepu* stan, int id_klienta) {
    return DodajPomijanego(stan, -id_klienta);
}

int CzyZmigrowany(StanSklepu* stan, int id_klienta) {
    return CzyPominiety(stan, -id_klienta);
}
