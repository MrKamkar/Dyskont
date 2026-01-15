#include "klient.h"
#include "wspolne.h"
#include "kasjer.h"
#include "kasa_samoobslugowa.h"

//Flaga trybu standalone (1 = uruchomiony recznie, 0 = spawned przez dyskont)
static int g_tryb_standalone = 0;
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
        
        //Symulacja namyslu (pomijana w trybie testu 1)
        SYMULACJA_USLEEP(stan_sklepu, (100 + (rand() % 401)) * 1000);

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
    return CzyMaKategorie(k, KAT_ALKOHOL);
}

double ObliczSumeKoszyka(const Klient* k) {
    if (!k) return 0.0;
    double suma = 0.0;
    for (int i = 0; i < k->liczba_produktow; i++) {
        suma += k->koszyk[i].cena;
    }
    return suma;
}

//Wydruk paragonu z lista produktow (tylko w trybie standalone)
void WydrukujParagon(const Klient* k, const char* typ_kasy, int id_kasy) {
    if (!k) return;
    
    //Wyswietl tylko w trybie standalone (uruchomiony recznie z terminala)
    if (!g_tryb_standalone) return;
    
    printf("\n========== PARAGON ==========\n");
    printf("Klient ID: %d | %s %d\n", k->id, typ_kasy, id_kasy);
    printf("-----------------------------\n");
    
    for (int i = 0; i < k->liczba_produktow; i++) {
        printf("  %s (%s) - %.2f PLN\n", 
                k->koszyk[i].nazwa, 
                NazwaKategorii(k->koszyk[i].kategoria),
                k->koszyk[i].cena);
    }
    
    printf("-----------------------------\n");
    printf("SUMA: %.2f PLN | Produktow: %d\n", ObliczSumeKoszyka(k), k->liczba_produktow);
    printf("==============================\n\n");
    fflush(stdout);
}

//Punkt wejscia dla procesu klienta
#ifdef KLIENT_STANDALONE
int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uzycie: %s <id_klienta>\n", argv[0]);
        return 1;
    }

    int id_klienta = atoi(argv[1]);
    
    //Jesli tylko 1 argument = tryb standalone (uruchomiony recznie)
    //Jesli 2 argumenty = spawned przez dyskont (argv[2] = "0")
    g_tryb_standalone = (argc == 2) ? 1 : 0;

    StanSklepu* stan_sklepu;
    int sem_id;
    
    if (InicjalizujProcesPochodny(&stan_sklepu, &sem_id, "Klient") == -1) {
        return 1;
    }

    srand(time(NULL) ^ getpid());
    Klient* klient = StworzKlienta(id_klienta);
    if (!klient) {
        fprintf(stderr, "Klient [%d]: Nie udalo sie stworzyc klienta\n", id_klienta);
        OdlaczPamiecWspoldzielona(stan_sklepu);
        return 1;
    }

    ZapiszLogF(LOG_INFO, "Klient [ID: %d] wszedl do sklepu. Wiek: %d lat. Planuje kupic %d produktow.", 
            klient->id, klient->wiek, klient->ilosc_planowana);

    ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    stan_sklepu->liczba_klientow_w_sklepie++;
    ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);

    ZapiszLog(LOG_INFO, "Klient rozpoczyna zakupy..");
    ZrobZakupy(klient, stan_sklepu);
    
    ZapiszLogF(LOG_INFO, "Klient [ID: %d] skonczyl zakupy. Produktow w koszyku: %d, Laczna kwota: %.2f PLN", 
            klient->id, klient->liczba_produktow, ObliczSumeKoszyka(klient));

    //Flaga czy klient moze dokonac zakupu i powod odmowy
    int kod_bledu = 0;
    int ma_alkohol = CzyZawieraAlkohol(klient);

    //Sprawdz czy nie ma ewakuacji
    ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    int ewakuacja = stan_sklepu->flaga_ewakuacji;
    ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    
    if (ewakuacja) {
        ZapiszLogF(LOG_OSTRZEZENIE, "Klient [ID: %d]: EWAKUACJA! Natychmiast opuszcza sklep.", klient->id);
        kod_bledu = 3;
        goto wyjscie;
    }

    int moze_kupic = 1;

    //Wybor kasy: 95% samoobslugowa, 5% stacjonarna
    int wybor_kasy = rand() % 100; //0-99
    int idzie_do_samoobslugowej = (wybor_kasy < 95);
    
    if (idzie_do_samoobslugowej) {
        //=== KASA SAMOOBSLUGOWA ===
        
        klient->stan = STAN_KOLEJKA_SAMOOBSLUGOWA;
        klient->czas_dolaczenia_do_kolejki = time(NULL);
        
        //Probuj dolaczic do kolejki samoobslugowej
        int dolaczyl = 0;
        
        if (DodajDoKolejkiSamoobslugowej(klient->id, stan_sklepu, sem_id) == 0) {
            dolaczyl = 1;
            ZapiszLogF(LOG_INFO, "Klient [ID: %d] dolaczyl do kolejki kas samoobslugowych.", klient->id);
        } else {
            ZapiszLogF(LOG_INFO, "Klient [ID: %d]: Kolejka samoobslugowa pelna, przechodzi do stacjonarnej.", klient->id);
            idzie_do_samoobslugowej = 0;
        }
        
        if (!dolaczyl) {
            //Nie dolaczyl, przejdz do stacjonarnej
            idzie_do_samoobslugowej = 0;
        } else {
            
            //Czekaj na wolna kase
            int id_kasy = -1;
            time_t czas_startu_oczekiwania = time(NULL);
            
            while (id_kasy == -1) {
                //Sprawdz ewakuacje
                ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
                ewakuacja = stan_sklepu->flaga_ewakuacji;
                ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
                if (ewakuacja) {
                    ZapiszLogF(LOG_OSTRZEZENIE, "Klient [ID: %d]: EWAKUACJA! Natychmiast opuszcza sklep.", klient->id);
                    moze_kupic = 0;
                    kod_bledu = 3;
                    goto wyjscie;
                }
                
                //Sprawdz czas oczekiwania
                time_t teraz = time(NULL);
                int timeout_kolejki = (stan_sklepu->tryb_testu == 1) ? 2 : MAX_CZAS_KOLEJKI;
                if ((teraz - czas_startu_oczekiwania) >= timeout_kolejki) {
                    ZapiszLogF(LOG_INFO, "Klient [ID: %d]: Przekroczono czas oczekiwania %d sek, przechodzi do stacjonarnej.",
                            klient->id, timeout_kolejki);
                    
                    ZajmijSemafor(sem_id, MUTEX_KOLEJKA_SAMO);
                    int znaleziono = UsunZKolejki(stan_sklepu->kolejka_samo, &stan_sklepu->liczba_w_kolejce_samo, klient->id);
                    ZwolnijSemafor(sem_id, MUTEX_KOLEJKA_SAMO);
                    
                    if (znaleziono) {
                        ZapiszLogF(LOG_INFO, "Klient [ID: %d]: Usunieto z kolejki samoobslugowej (pozostalo: %d).",
                                klient->id, stan_sklepu->liczba_w_kolejce_samo);
                    }
                    
                    idzie_do_samoobslugowej = 0;
                    break;
                }
                
                struct timespec timeout = {1, 0};
                struct sembuf op = {SEM_WOLNE_KASY_SAMO, -1, 0};
                if (semtimedop(sem_id, &op, 1, &timeout) == 0) {
                    //Semafor zajety - jest wolna kasa
                    id_kasy = ZnajdzWolnaKase(stan_sklepu, sem_id);
                    if (id_kasy == -1) {
                        //Ktos inny zajal kase, oddaj semafor i probuj ponownie
                        ZwolnijSemafor(sem_id, SEM_WOLNE_KASY_SAMO);
                    }
                }
                //Jesli timeout (EAGAIN) - kontynuuj petle, sprawdzi ewakuacje/timeout
            }
            
            if (id_kasy != -1 && idzie_do_samoobslugowej) {
                //Zajmij kase
                if (ZajmijKase(id_kasy, klient->id, stan_sklepu, sem_id) == 0) {
                    //Usun siebie z kolejki samoobslugowej (bo zajmujemy kase)
                    UsunKlientaZKolejkiSamoobslugowej(klient->id, stan_sklepu, sem_id);
                    
                    klient->stan = STAN_PRZY_KASIE;
                    
                    //Obsluz klienta przy kasie samoobslugowej
                    int wynik_obslugi = ObsluzKlientaSamoobslugowo(id_kasy, klient->id, klient->liczba_produktow,
                                               ObliczSumeKoszyka(klient), ma_alkohol, klient->wiek,
                                               stan_sklepu, sem_id);
                    
                    if (wynik_obslugi == -1) {
                        ZapiszLogF(LOG_INFO, "Klient [ID: %d]: Timeout blokady kasy, przechodzi do stacjonarnej.", klient->id);
                        idzie_do_samoobslugowej = 0;
                    } else if (wynik_obslugi == -2) {
                        moze_kupic = 0;
                        kod_bledu = 1;
                        ZwolnijKase(id_kasy, stan_sklepu, sem_id);
                    } else if (wynik_obslugi == -3) {
                        moze_kupic = 0;
                        kod_bledu = 3;
                        goto wyjscie;
                    } else {
                        //Sukces - wydrukuj paragon i zwolnij kase
                        WydrukujParagon(klient, "Kasa samoobslugowa", id_kasy + 1);
                        ZwolnijKase(id_kasy, stan_sklepu, sem_id);
                    }
                } else {
                    ZapiszLogF(LOG_OSTRZEZENIE, "Klient [ID: %d]: Nie udalo sie zajac kasy samoobslugowej, przechodzi do stacjonarnej.", klient->id);
                    idzie_do_samoobslugowej = 0;
                }
            }
        }
    }
    
    if (!idzie_do_samoobslugowej && moze_kupic) {
        ZapiszLogF(LOG_INFO, "Klient [ID: %d] wybiera kase stacjonarna.", klient->id);
        
        //Sprawdz status obu kas stacjonarnych
        ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
        StanKasy stan_kasy1 = stan_sklepu->kasy_stacjonarne[0].stan;
        StanKasy stan_kasy2 = stan_sklepu->kasy_stacjonarne[1].stan;
        int kolejka1 = stan_sklepu->kasy_stacjonarne[0].liczba_w_kolejce;
        int kolejka2 = stan_sklepu->kasy_stacjonarne[1].liczba_w_kolejce;
        ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
        
        int kasa1_otwarta = (stan_kasy1 != KASA_ZAMKNIETA && stan_kasy1 != KASA_ZAMYKANA);
        int kasa2_otwarta = (stan_kasy2 != KASA_ZAMKNIETA && stan_kasy2 != KASA_ZAMYKANA);
        int kasa2_pelna = (kolejka2 >= MAX_KOLEJKA_STACJONARNA);
        
        int wybrana_kasa = -1;
        
        if (kasa2_otwarta && !kasa2_pelna) {
            //Kasa 2 jest glowna i ma miejsce
            if (kasa1_otwarta && kolejka1 < kolejka2) {
                wybrana_kasa = 0;
            } else {
                wybrana_kasa = 1;
            }
        } else if (kasa1_otwarta) {
            //Kasa 2 zamknieta lub pelna, Kasa 1 otwarta => ustaw sie do Kasy 1
            wybrana_kasa = 0;
        } else if (kasa2_otwarta && kasa2_pelna) {
            //Kasa 2 otwarta ale pelna, Kasa 1 zamknieta => ustaw sie do Kasy 1
            wybrana_kasa = 0;
        } else if (kasa2_otwarta) {
            //Kasa 2 otwarta, Kasa 1 zamknieta => ustaw sie do Kasy 2
            wybrana_kasa = 1;
        } else {
            //Obie zamkniete - ustaw sie do Kasy 1
            wybrana_kasa = 0;
        }
        
        if (wybrana_kasa == -1) {
            ZapiszLogF(LOG_OSTRZEZENIE, "Klient [ID: %d]: Brak dostepnych kas stacjonarnych!", klient->id);
            moze_kupic = 0;
            kod_bledu = 2;
        } else {
            //Dolacz do wybranej kolejki
            klient->stan = (wybrana_kasa == 0) ? STAN_KOLEJKA_KASA_1 : STAN_KOLEJKA_KASA_2;
            klient->czas_dolaczenia_do_kolejki = time(NULL);
            
            if (DodajDoKolejkiStacjonarnej(wybrana_kasa, klient->id, stan_sklepu, sem_id) == 0) {
                ZapiszLogF(LOG_INFO, "Klient [ID: %d] dolaczyl do kolejki kasy stacjonarnej %d.", klient->id, wybrana_kasa + 1);
                
                //Czekaj na obsluge (pomijane w trybie testu 1)
                SYMULACJA_USLEEP(stan_sklepu, (2 + (rand() % 5)) * 1000000);
                
                klient->stan = STAN_PRZY_KASIE;
                
                if (ma_alkohol) {
                    if (klient->wiek < 18) {
                        ZapiszLogF(LOG_BLAD, "Kasa stacjonarna: ODMOWA! Klient [ID: %d] niepelnoletni (wiek: %d)",
                                klient->id, klient->wiek);
                        moze_kupic = 0;
                        kod_bledu = 1;
                    } else {
                        ZapiszLogF(LOG_INFO, "Kasa stacjonarna: Weryfikacja wieku OK [ID: %d, wiek: %d]", klient->id, klient->wiek);
                    }
                }
                
                if (moze_kupic) {
                    WydrukujParagon(klient, "Kasa stacjonarna", wybrana_kasa + 1);
                    ZapiszLogF(LOG_INFO, "Klient [ID: %d] zostal obsluzony przy kasie stacjonarnej. Suma: %.2f PLN",
                            klient->id, ObliczSumeKoszyka(klient));
                }
            } else {
                ZapiszLogF(LOG_OSTRZEZENIE, "Klient [ID: %d]: Kolejka stacjonarna pelna!", klient->id);
                moze_kupic = 0;
                kod_bledu = 2;
            }
        }
    }
    
wyjscie:
    klient->stan = STAN_WYJSCIE;
    
    //Najpierw zaloguj wyjscie (przed operacja na semaforze)
    if (moze_kupic) {
        ZapiszLogF(LOG_INFO, "Klient [ID: %d] opuscil sklep po dokonaniu zakupow.", klient->id);
    } else {
        const char* powod;
        switch (kod_bledu) {
            case 1: powod = "niepelnoletni"; break;
            case 2: powod = "kolejka pelna"; break;
            case 3: powod = "ewakuacja"; break;
            default: powod = "nieznany"; break;
        }
        ZapiszLogF(LOG_BLAD, "Klient [ID: %d] opuscil sklep BEZ zakupow (powod: %s).", klient->id, powod);
    }
    
    //Zmniejsz licznik klientow (moze sie nie udac jesli semafory usuniete)
    ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    stan_sklepu->liczba_klientow_w_sklepie--;
    ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);

    UsunKlienta(klient);
    OdlaczPamiecWspoldzielona(stan_sklepu);

    return kod_bledu;
}
#endif

