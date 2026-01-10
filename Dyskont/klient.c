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

//Punkt wejscia dla procesu klienta
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
    
    //Inicjalizacja systemu logowania (dolaczenie do kolejki komunikatow)
    InicjalizujSystemLogowania(sciezka);

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

    //Sekcja krytyczna, zwiekszenie licznika klientow
    ZajmijSemafor(sem_id, SEM_PAMIEC_WSPOLDZIELONA);
    stan_sklepu->liczba_klientow_w_sklepie++;
    ZwolnijSemafor(sem_id, SEM_PAMIEC_WSPOLDZIELONA);

    //Zakupy bez sekcji krytycznej, tylko odczyt z magazynu
    ZapiszLog(LOG_INFO, "Klient rozpoczyna zakupy..");
    ZrobZakupy(klient, stan_sklepu);
    
    sprintf(buf, "Klient [ID: %d] skonczyl zakupy. Produktow w koszyku: %d, Laczna kwota: %.2f PLN", 
            klient->id, klient->liczba_produktow, ObliczSumeKoszyka(klient));
    ZapiszLog(LOG_INFO, buf);

    //Flaga czy klient moze dokonac zakupu i powod odmowy
    int moze_kupic = 1;
    int kod_bledu = 0;  //0=sukces, 1=niepelnoletni, 2=kolejka pelna, 3=ewakuacja
    int ma_alkohol = CzyZawieraAlkohol(klient);

    //Sprawdz czy nie ma ewakuacji
    ZajmijSemafor(sem_id, SEM_PAMIEC_WSPOLDZIELONA);
    int ewakuacja = stan_sklepu->flaga_ewakuacji;
    ZwolnijSemafor(sem_id, SEM_PAMIEC_WSPOLDZIELONA);
    
    if (ewakuacja) {
        sprintf(buf, "Klient [ID: %d]: EWAKUACJA! Natychmiast opuszcza sklep.", klient->id);
        ZapiszLog(LOG_OSTRZEZENIE, buf);
        moze_kupic = 0;
        kod_bledu = 3;  //Ewakuacja
        goto wyjscie;
    }

    //Wybor kasy: 95% samoobslugowa, 5% stacjonarna
    int wybor_kasy = rand() % 100; //0-99
    int idzie_do_samoobslugowej = (wybor_kasy < 95);
    
    if (idzie_do_samoobslugowej) {
        //=== KASA SAMOOBSLUGOWA ===
        
        klient->stan = STAN_KOLEJKA_SAMOOBSLUGOWA;
        klient->czas_dolaczenia_do_kolejki = time(NULL);
        
        //Probuj dolaczic do kolejki (czekaj jesli pelna)
        time_t czas_startu = time(NULL);
        int dolaczyl = 0;
        
        while (!dolaczyl && (time(NULL) - czas_startu) < MAX_CZAS_KOLEJKI) {
            //Sprawdz ewakuacje
            ZajmijSemafor(sem_id, SEM_PAMIEC_WSPOLDZIELONA);
            ewakuacja = stan_sklepu->flaga_ewakuacji;
            ZwolnijSemafor(sem_id, SEM_PAMIEC_WSPOLDZIELONA);
            if (ewakuacja) {
                sprintf(buf, "Klient [ID: %d]: EWAKUACJA! Natychmiast opuszcza sklep.", klient->id);
                ZapiszLog(LOG_OSTRZEZENIE, buf);
                moze_kupic = 0;
                kod_bledu = 3;
                goto wyjscie;
            }
            
            if (DodajDoKolejkiSamoobslugowej(klient->id, stan_sklepu, sem_id) == 0) {
                dolaczyl = 1;
                sprintf(buf, "Klient [ID: %d] dolaczyl do kolejki kas samoobslugowych.", klient->id);
                ZapiszLog(LOG_INFO, buf);
            } else {
                //Kolejka pelna, poczekaj i sprobuj ponownie
                usleep(500000); //500ms
            }
        }
        
        if (!dolaczyl) {
            //Timeout, przejdz do stacjonarnej
            sprintf(buf, "Klient [ID: %d]: Nie udalo sie dolaczyc do kolejki, przechodzi do stacjonarnej.", klient->id);
            ZapiszLog(LOG_INFO, buf);
            idzie_do_samoobslugowej = 0;
        } else {
            
            //Czekaj na wolna kase z timeoutem T
            int id_kasy = -1;
            time_t czas_startu_oczekiwania = time(NULL);
            
            while (id_kasy == -1) {
                //Sprawdz ewakuacje
                ZajmijSemafor(sem_id, SEM_PAMIEC_WSPOLDZIELONA);
                ewakuacja = stan_sklepu->flaga_ewakuacji;
                ZwolnijSemafor(sem_id, SEM_PAMIEC_WSPOLDZIELONA);
                if (ewakuacja) {
                    sprintf(buf, "Klient [ID: %d]: EWAKUACJA! Natychmiast opuszcza sklep.", klient->id);
                    ZapiszLog(LOG_OSTRZEZENIE, buf);
                    moze_kupic = 0;
                    kod_bledu = 3;
                    goto wyjscie;
                }
                
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
                    
                    //Weryfikacja wieku przy alkoholu, jesli niepelnoletni odmowa
                    if (ma_alkohol && klient->wiek < 18) {
                        moze_kupic = 0;
                        kod_bledu = 1;  //Niepelnoletni
                    }
                    
                    //Zwolnij kase
                    ZwolnijKase(id_kasy, stan_sklepu, sem_id);
                } else {
                    //Nie udalo sie zajac kasy, sprobuj stacjonarnej
                    sprintf(buf, "Klient [ID: %d]: Nie udalo sie zajac kasy samoobslugowej, przechodzi do stacjonarnej.", klient->id);
                    ZapiszLog(LOG_OSTRZEZENIE, buf);
                    idzie_do_samoobslugowej = 0;
                }
            }
        }
    }
    
    // KASA STACJONARNA (5% lub po przekroczeniu timeout)
    if (!idzie_do_samoobslugowej && moze_kupic) {
        sprintf(buf, "Klient [ID: %d] wybiera kase stacjonarna.", klient->id);
        ZapiszLog(LOG_INFO, buf);
        
        //Sprawdz status obu kas stacjonarnych
        ZajmijSemafor(sem_id, SEM_PAMIEC_WSPOLDZIELONA);
        StanKasy stan_kasy1 = stan_sklepu->kasy_stacjonarne[0].stan;
        StanKasy stan_kasy2 = stan_sklepu->kasy_stacjonarne[1].stan;
        int kolejka1 = stan_sklepu->kasy_stacjonarne[0].liczba_w_kolejce;
        int kolejka2 = stan_sklepu->kasy_stacjonarne[1].liczba_w_kolejce;
        
        //Jesli > 3 osoby w kolejce i kasa 1 zamknieta, otworz ja
        if (stan_kasy1 == KASA_ZAMKNIETA && kolejka1 > 3) {
            stan_sklepu->kasy_stacjonarne[0].stan = KASA_WOLNA;
            stan_sklepu->kasy_stacjonarne[0].czas_ostatniej_obslugi = time(NULL);
            stan_kasy1 = KASA_WOLNA;
            ZapiszLog(LOG_INFO, "Kasa stacjonarna 1 zostala otwarta (> 3 osoby czekaja).");
        }
        ZwolnijSemafor(sem_id, SEM_PAMIEC_WSPOLDZIELONA);
        
        //Wybor kasy: jesli obie otwarte, wybierz krotsza kolejke
        int wybrana_kasa = 0;  //Domyslnie kasa 1
        
        int kasa1_dostepna = (stan_kasy1 == KASA_WOLNA || stan_kasy1 == KASA_ZAJETA);
        int kasa2_dostepna = (stan_kasy2 == KASA_WOLNA || stan_kasy2 == KASA_ZAJETA);
        
        if (kasa1_dostepna && kasa2_dostepna) {
            //Obie kasy otwarte, wybierz krotsza kolejke
            if (kolejka2 < kolejka1) {
                wybrana_kasa = 1;  //Kasa 2
            }
        } else if (kasa2_dostepna && !kasa1_dostepna) {
            wybrana_kasa = 1;  //Tylko kasa 2 dostepna
        }
        
        //Dolacz do wybranej kolejki
        klient->stan = (wybrana_kasa == 0) ? STAN_KOLEJKA_KASA_1 : STAN_KOLEJKA_KASA_2;
        klient->czas_dolaczenia_do_kolejki = time(NULL);
        
        if (DodajDoKolejkiStacjonarnej(wybrana_kasa, klient->id, stan_sklepu, sem_id) == 0) {
            sprintf(buf, "Klient [ID: %d] dolaczyl do kolejki kasy stacjonarnej %d.", klient->id, wybrana_kasa + 1);
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
                    kod_bledu = 1;  //Niepelnoletni
                } else {
                    sprintf(buf, "Kasa stacjonarna: Weryfikacja wieku OK [ID: %d, wiek: %d]", klient->id, klient->wiek);
                    ZapiszLog(LOG_INFO, buf);
                }
            }
            
            if (moze_kupic) {
                sprintf(buf, "Klient [ID: %d] zostal obsluzony przy kasie stacjonarnej. Suma: %.2f PLN",
                        klient->id, ObliczSumeKoszyka(klient));
                ZapiszLog(LOG_INFO, buf);
            }
        } else {
            sprintf(buf, "Klient [ID: %d]: Kolejka stacjonarna pelna!", klient->id);
            ZapiszLog(LOG_OSTRZEZENIE, buf);
            moze_kupic = 0;
            kod_bledu = 2;  //Kolejka pelna
        }
    }
    
wyjscie:
    klient->stan = STAN_WYJSCIE;
    
    //Sekcja krytyczna, zmniejszenie licznika klientow
    ZajmijSemafor(sem_id, SEM_PAMIEC_WSPOLDZIELONA);
    stan_sklepu->liczba_klientow_w_sklepie--;
    ZwolnijSemafor(sem_id, SEM_PAMIEC_WSPOLDZIELONA);

    if (moze_kupic) {
        sprintf(buf, "Klient [ID: %d] opuscil sklep po dokonaniu zakupow.", klient->id);
    } else {
        const char* powod;
        switch (kod_bledu) {
            case 1: powod = "niepelnoletni"; break;
            case 2: powod = "kolejka pelna"; break;
            case 3: powod = "ewakuacja"; break;
            default: powod = "nieznany"; break;
        }
        sprintf(buf, "Klient [ID: %d] opuscil sklep BEZ zakupow (powod: %s).", klient->id, powod);
    }
    ZapiszLog(LOG_INFO, buf);

    //Czyszczenie
    UsunKlienta(klient);
    OdlaczPamiecWspoldzielona(stan_sklepu);

    return kod_bledu;  //0=sukces, 1=niepelnoletni, 2=kolejka pelna, 3=ewakuacja
}
#endif

