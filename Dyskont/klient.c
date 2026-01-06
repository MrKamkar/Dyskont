#include "klient.h"
#include "semafory.h"
#include "logi.h"
#include "kasjer.h"
#include "kasa_samoobslugowa.h"
//Sprawdzenie czy kategoria istnieje w koszyku
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
    k->wiek = 6 + (rand() % 85); //od 6 do 90 lat
    
    //Losujemy od razu ile kupi (3-10)
    k->ilosc_planowana = 3 + (rand() % 8);
    k->koszyk = (Produkt*)malloc(sizeof(Produkt) * k->ilosc_planowana);
    
    //Obsluga bledu alokacji
    if (!k->koszyk) {
        free(k);
        return NULL;
    }

    //Na start koszyk jest pusty (fizycznie), ale miejsce zaklepane
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

    //Po prostu wypelniamy koszyk do zaplanowanej ilosci
    while (k->liczba_produktow < k->ilosc_planowana) {
        
        //Symulacja namyslu
        int czas_namyslu_ms = 100 + (rand() % 401);
        usleep(czas_namyslu_ms * 1000);  //od 100 do 500 ms

        //Wybor produktu (roznorodnosc)
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

//Funkcja main - punkt wejscia dla procesu klienta
#ifdef KLIENT_STANDALONE
int main(int argc, char* argv[]) {
    //Sprawdzenie argumentow
    if (argc != 3) {
        fprintf(stderr, "Uzycie: %s <sciezka> <id_klienta>\n", argv[0]);
        return 1;
    }

    const char* sciezka = argv[1];
    int id_klienta = atoi(argv[2]);

    //Dolaczenie do pamieci wspoldzielonej
    StanSklepu* stan_sklepu = DolaczPamiecWspoldzielona(sciezka);
    if (!stan_sklepu) {
        fprintf(stderr, "Klient [%d]: Nie mozna dolaczyc do pamieci wspoldzielonej\n", id_klienta);
        return 1;
    }

    //Dolaczenie do semaforow
    int sem_id = DolaczSemafory(sciezka);
    if (sem_id == -1) {
        fprintf(stderr, "Klient [%d]: Nie mozna dolaczyc do semaforow\n", id_klienta);
        OdlaczPamiecWspoldzielona(stan_sklepu);
        return 1;
    }

    //Utworzenie klienta
    srand(time(NULL) ^ getpid()); //Seed dla losowosci
    Klient* klient = StworzKlienta(id_klienta);
    if (!klient) {
        fprintf(stderr, "Klient [%d]: Nie udalo sie stworzyc klienta\n", id_klienta);
        OdlaczPamiecWspoldzielona(stan_sklepu);
        return 1;
    }

    //Logowanie wejscia
    char buf[256];
    sprintf(buf, "Klient [ID: %d] wszedl do sklepu. Wiek: %d lat. Planuje kupic %d produktow.", 
            klient->id, klient->wiek, klient->ilosc_planowana);
    ZapiszLog(LOG_INFO, buf);

    //Sekcja krytyczna - zwiekszenie licznika klientow
    ZajmijSemafor(sem_id, SEM_PAMIEC_WSPOLDZIELONA);
    stan_sklepu->liczba_klientow_w_sklepie++;
    ZwolnijSemafor(sem_id, SEM_PAMIEC_WSPOLDZIELONA);

    //Zakupy (bez sekcji krytycznej - tylko odczyt z magazynu)
    ZapiszLog(LOG_INFO, "Klient rozpoczyna zakupy..");
    ZrobZakupy(klient, stan_sklepu);
    
    sprintf(buf, "Klient [ID: %d] zakonczyl zakupy. Produktow w koszyku: %d, Laczna kwota: %.2f PLN", 
            klient->id, klient->liczba_produktow, ObliczSumeKoszyka(klient));
    ZapiszLog(LOG_INFO, buf);

    //Flaga czy klient moze dokonac zakupu
    int moze_kupic = 1;
    int ma_alkohol = CzyZawieraAlkohol(klient);

    //Wybor kasy: 95% samoobslugowa, 5% stacjonarna
    int wybor_kasy = rand() % 100; //0-99
    int idzie_do_samoobslugowej = (wybor_kasy < 95);
    
    if (idzie_do_samoobslugowej) {
        //=== KASA SAMOOBSLUGOWA ===
        sprintf(buf, "Klient [ID: %d] wybiera kase samoobslugowa.", klient->id);
        ZapiszLog(LOG_INFO, buf);
        
        klient->stan = STAN_KOLEJKA_SAMOOBSLUGOWA;
        klient->czas_dolaczenia_do_kolejki = time(NULL);
        
        //Dolacz do wspolnej kolejki
        if (DodajDoKolejkiSamoobslugowej(klient->id, stan_sklepu, sem_id) != 0) {
            sprintf(buf, "Klient [ID: %d]: Kolejka samoobslugowa pelna!", klient->id);
            ZapiszLog(LOG_OSTRZEZENIE, buf);
            moze_kupic = 0;
        } else {
            sprintf(buf, "Klient [ID: %d] dolaczyl do kolejki kas samoobslugowych.", klient->id);
            ZapiszLog(LOG_INFO, buf);
            
            //Czekaj na wolna kase z timeoutem T
            int id_kasy = -1;
            time_t czas_startu_oczekiwania = time(NULL);
            
            while (id_kasy == -1) {
                //Sprawdz czy nie przekroczono czasu T
                time_t teraz = time(NULL);
                if ((teraz - czas_startu_oczekiwania) >= MAX_CZAS_KOLEJKI) {
                    sprintf(buf, "Klient [ID: %d]: Przekroczono czas oczekiwania %d sek, przechodzi do stacjonarnej.",
                            klient->id, MAX_CZAS_KOLEJKI);
                    ZapiszLog(LOG_INFO, buf);
                    
                    //Usun z kolejki samoobslugowej
                    idzie_do_samoobslugowej = 0;
                    break;
                }
                
                //Szukaj wolnej kasy
                id_kasy = ZnajdzWolnaKase(stan_sklepu, sem_id);
                if (id_kasy == -1) {
                    usleep(200000); //200ms
                }
            }
            
            if (id_kasy != -1 && idzie_do_samoobslugowej) {
                //Zajmij kase
                if (ZajmijKase(id_kasy, klient->id, stan_sklepu, sem_id) == 0) {
                    klient->stan = STAN_PRZY_KASIE;
                    
                    //Obsluz klienta przy kasie samoobslugowej
                    ObsluzKlientaSamoobslugowo(id_kasy, klient->id, klient->liczba_produktow,
                                               ObliczSumeKoszyka(klient), ma_alkohol, klient->wiek,
                                               stan_sklepu, sem_id);
                    
                    //Weryfikacja wieku przy alkoholu - jesli niepelnoletni, odmowa
                    if (ma_alkohol && klient->wiek < 18) {
                        moze_kupic = 0;
                    }
                    
                    //Zwolnij kase
                    ZwolnijKase(id_kasy, stan_sklepu, sem_id);
                }
            }
        }
    }
    
    //=== KASA STACJONARNA (5% lub po przekroczeniu timeout) ===
    if (!idzie_do_samoobslugowej && moze_kupic) {
        sprintf(buf, "Klient [ID: %d] wybiera kase stacjonarna.", klient->id);
        ZapiszLog(LOG_INFO, buf);
        
        //Sprawdz czy kasa 1 jest otwarta, jesli nie - otworz gdy > 3 osoby czeka
        ZajmijSemafor(sem_id, SEM_PAMIEC_WSPOLDZIELONA);
        StanKasy stan_kasy1 = stan_sklepu->kasy_stacjonarne[0].stan;
        int w_kolejce = stan_sklepu->kasy_stacjonarne[0].liczba_w_kolejce;
        
        //Jesli > 3 osoby w kolejce i kasa zamknieta, otworz ja
        if (stan_kasy1 == KASA_ZAMKNIETA && w_kolejce >= 3) {
            stan_sklepu->kasy_stacjonarne[0].stan = KASA_WOLNA;
            stan_sklepu->kasy_stacjonarne[0].czas_ostatniej_obslugi = time(NULL);
            ZapiszLog(LOG_INFO, "Kasa stacjonarna 1 zostala otwarta (> 3 osoby czekaja).");
        }
        ZwolnijSemafor(sem_id, SEM_PAMIEC_WSPOLDZIELONA);
        
        //Dolacz do kolejki kasy 1
        klient->stan = STAN_KOLEJKA_KASA_1;
        klient->czas_dolaczenia_do_kolejki = time(NULL);
        
        if (DodajDoKolejkiStacjonarnej(0, klient->id, stan_sklepu, sem_id) == 0) {
            sprintf(buf, "Klient [ID: %d] dolaczyl do kolejki kasy stacjonarnej 1.", klient->id);
            ZapiszLog(LOG_INFO, buf);
            
            //Czekaj na obsluge
            int czas_oczekiwania = 2 + (rand() % 5); //2-6 sekund
            sleep(czas_oczekiwania);
            
            klient->stan = STAN_PRZY_KASIE;
            
            //Weryfikacja alkoholu przy kasie stacjonarnej
            if (ma_alkohol) {
                if (klient->wiek < 18) {
                    sprintf(buf, "Kasa stacjonarna: ODMOWA! Klient [ID: %d] niepelnoletni (wiek: %d)",
                            klient->id, klient->wiek);
                    ZapiszLog(LOG_BLAD, buf);
                    moze_kupic = 0;
                } else {
                    sprintf(buf, "Kasa stacjonarna: Weryfikacja wieku OK [ID: %d, wiek: %d]",
                            klient->id, klient->wiek);
                    ZapiszLog(LOG_INFO, buf);
                }
            }
            
            if (moze_kupic) {
                sprintf(buf, "Klient [ID: %d] zostal obsluzony przy kasie stacjonarnej. Suma: %.2f PLN",
                        klient->id, ObliczSumeKoszyka(klient));
                ZapiszLog(LOG_INFO, buf);
            }
        } else {
            sprintf(buf, "Klient [ID: %d]: Kolejka pelna! Nie moze dokonac zakupu.", klient->id);
            ZapiszLog(LOG_OSTRZEZENIE, buf);
        }
    }
    
    klient->stan = STAN_WYJSCIE;
    
    //Sekcja krytyczna - zmniejszenie licznika klientow
    ZajmijSemafor(sem_id, SEM_PAMIEC_WSPOLDZIELONA);
    stan_sklepu->liczba_klientow_w_sklepie--;
    ZwolnijSemafor(sem_id, SEM_PAMIEC_WSPOLDZIELONA);

    sprintf(buf, "Klient [ID: %d] opuscil sklep.", klient->id);
    ZapiszLog(LOG_INFO, buf);

    //Kod wyjscia: 0 = sukces, 1 = odmowa (np. niepelnoletni z alkoholem)
    int kod_wyjscia = moze_kupic ? 0 : 1;

    //Czyszczenie
    UsunKlienta(klient);
    OdlaczPamiecWspoldzielona(stan_sklepu);

    return kod_wyjscia;
}
#endif

