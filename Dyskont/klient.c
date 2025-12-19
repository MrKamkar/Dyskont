#include "klient.h"
#include "semafory.h"
#include "logi.h"


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
    
    // Obsluga bledu alokacji
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

    // Po prostu wypelniamy koszyk do zaplanowanej ilosci
    while (k->liczba_produktow < k->ilosc_planowana) {
        
        // Symulacja namyslu
        int czas_namyslu_ms = 100 + (rand() % 401);
        usleep(czas_namyslu_ms * 1000);  // od 100 do 500 ms

        // Wybor produktu (roznorodnosc)
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

// Funkcja main - punkt wejscia dla procesu klienta
#ifdef KLIENT_STANDALONE
int main(int argc, char* argv[]) {
    // Sprawdzenie argumentow
    if (argc != 3) {
        fprintf(stderr, "Uzycie: %s <sciezka> <id_klienta>\n", argv[0]);
        return 1;
    }

    const char* sciezka = argv[1];
    int id_klienta = atoi(argv[2]);

    // Dolaczenie do pamieci wspoldzielonej
    StanSklepu* stan_sklepu = DolaczPamiecWspoldzielona(sciezka);
    if (!stan_sklepu) {
        fprintf(stderr, "Klient [%d]: Nie mozna dolaczyc do pamieci wspoldzielonej\n", id_klienta);
        return 1;
    }

    // Dolaczenie do semaforow
    int sem_id = DolaczSemafory(sciezka);
    if (sem_id == -1) {
        fprintf(stderr, "Klient [%d]: Nie mozna dolaczyc do semaforow\n", id_klienta);
        OdlaczPamiecWspoldzielona(stan_sklepu);
        return 1;
    }

    // Utworzenie klienta
    srand(time(NULL) ^ getpid()); // Seed dla losowosci
    Klient* klient = StworzKlienta(id_klienta);
    if (!klient) {
        fprintf(stderr, "Klient [%d]: Nie udalo sie stworzyc klienta\n", id_klienta);
        OdlaczPamiecWspoldzielona(stan_sklepu);
        return 1;
    }

    // Logowanie wejscia
    char buf[256];
    sprintf(buf, "Klient [ID: %d] wszedl do sklepu. Wiek: %d lat. Planuje kupic %d produktow.", 
            klient->id, klient->wiek, klient->ilosc_planowana);
    ZapiszLog(LOG_INFO, buf);

    // Sekcja krytyczna - zwiekszenie licznika klientow
    ZajmijSemafor(sem_id, SEM_PAMIEC_WSPOLDZIELONA);
    stan_sklepu->liczba_klientow_w_sklepie++;
    ZwolnijSemafor(sem_id, SEM_PAMIEC_WSPOLDZIELONA);

    // Zakupy (bez sekcji krytycznej - tylko odczyt z magazynu)
    ZapiszLog(LOG_INFO, "Klient rozpoczyna zakupy...");
    ZrobZakupy(klient, stan_sklepu);
    
    sprintf(buf, "Klient [ID: %d] zakonczyl zakupy. Produktow w koszyku: %d, Laczna kwota: %.2f PLN", 
            klient->id, klient->liczba_produktow, ObliczSumeKoszyka(klient));
    ZapiszLog(LOG_INFO, buf);

    // Weryfikacja alkoholu
    if (CzyZawieraAlkohol(klient)) {
        if (klient->wiek < 18) {
            sprintf(buf, "ALARM: Klient [ID: %d] niepelnoletni (wiek: %d) probuje kupic alkohol!", 
                    klient->id, klient->wiek);
            ZapiszLog(LOG_BLAD, buf);
        } else {
            sprintf(buf, "Klient [ID: %d]: Weryfikacja wieku OK (wiek: %d)", klient->id, klient->wiek);
            ZapiszLog(LOG_INFO, buf);
        }
    }

    // Sekcja krytyczna - zmniejszenie licznika klientow
    ZajmijSemafor(sem_id, SEM_PAMIEC_WSPOLDZIELONA);
    stan_sklepu->liczba_klientow_w_sklepie--;
    ZwolnijSemafor(sem_id, SEM_PAMIEC_WSPOLDZIELONA);

    sprintf(buf, "Klient [ID: %d] opuscil sklep.", klient->id);
    ZapiszLog(LOG_INFO, buf);

    // Czyszczenie
    UsunKlienta(klient);
    OdlaczPamiecWspoldzielona(stan_sklepu);

    return 0;
}
#endif

