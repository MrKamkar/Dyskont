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



void ZrobZakupy(Klient* k, const StanSklepu* stan_sklepu) {
    if (!k || !k->koszyk || !stan_sklepu) return;

    //Po prostu wypelniamy koszyk do zaplanowanej ilosci
    while (k->liczba_produktow < k->ilosc_planowana) {
        
        //Sprawdz ewakuacje lub SIGTERM
        if (CZY_KONCZYC(stan_sklepu)) {
            return;  //Przerwij zakupy
        }

        //Symulacja chodzenia po sklepie i wyboru produktu (2-5 sekund)
        SYMULACJA_USLEEP(stan_sklepu, (2000000 + (rand() % 3000001)));

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
    
    //Tryb standalone lub spawned przez dyskont
    g_tryb_standalone = (argc == 2) ? 1 : 0;

    StanSklepu* stan_sklepu;
    int sem_id;
    
    if (InicjalizujProcesPochodny(&stan_sklepu, &sem_id, "Klient") == -1) {
        return 1;
    }

    //Pobierz ID wspólnej kolejki komunikatów
    int msg_id = msgget(KLUCZ_KOLEJKI, 0600);
    if (msg_id == -1) {
        ZapiszLog(LOG_BLAD, "Klient: Blad dolaczenia do kolejki komunikatow!");
        OdlaczPamiecWspoldzielona(stan_sklepu);
        return 1;
    }
    
    
    //Ustawia g_sygnal_wyjscia (przerywa semafory) - obsluga SIGTERM i SIGQUIT
    struct sigaction sa;
    sa.sa_handler = ObslugaSygnaluWyjscia;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; // Brak SA_RESTART - przerywa syscalls (usleep, semop)
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);  // SIGQUIT jako alias dla SIGTERM


    srand(time(NULL) ^ getpid());
    Klient* klient = StworzKlienta(id_klienta);
    if (!klient) {
        fprintf(stderr, "Klient [%d]: Nie udalo sie stworzyc klienta\n", id_klienta);
        OdlaczPamiecWspoldzielona(stan_sklepu);
        return 1;
    }

    ZapiszLogF(LOG_INFO, "Klient [ID: %d] wszedl do sklepu. Wiek: %d lat. Planuje kupic %d produktow.", 
            klient->id, klient->wiek, klient->ilosc_planowana);

    if (ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA) == 0) {
        stan_sklepu->liczba_klientow_w_sklepie++;
        ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    }

    ZapiszLog(LOG_INFO, "Klient rozpoczyna zakupy..");
    ZrobZakupy(klient, stan_sklepu);
    
    ZapiszLogF(LOG_INFO, "Klient [ID: %d] skonczyl zakupy. Produktow w koszyku: %d, Laczna kwota: %.2f PLN", 
            klient->id, klient->liczba_produktow, ObliczSumeKoszyka(klient));

    //Flaga czy klient moze dokonac zakupu i powod odmowy
    int kod_bledu = 0;
    int ma_alkohol = CzyZawieraAlkohol(klient);
    int moze_kupic = 1;

    //Sprawdz czy nie ma ewakuacji lub SIGTERM
    if (CZY_KONCZYC(stan_sklepu)) {
        ZapiszLogF(LOG_OSTRZEZENIE, "Klient [ID: %d] przerywa zakupy (powod: EWAKUACJA).", klient->id);
        kod_bledu = 3;
        moze_kupic = 0;
    }

    //Wybor kasy: 95% samoobslugowa, 5% stacjonarna
    int wybor_kasy = rand() % 100; //0-99
    int idzie_do_samoobslugowej = (wybor_kasy < 95);
    
    //=== KASA SAMOOBSLUGOWA ===
    if (moze_kupic && idzie_do_samoobslugowej) {
        klient->stan = STAN_KOLEJKA_SAMOOBSLUGOWA;
        klient->czas_dolaczenia_do_kolejki = time(NULL);
        
        //Wyslij zgloszenie do kolejki samoobslugowej (mtype=3)
        Komunikat msg_req = {0};
        msg_req.mtype = 3;
        msg_req.id_klienta = klient->id;
        msg_req.liczba_produktow = klient->liczba_produktow;
        msg_req.suma_koszyka = ObliczSumeKoszyka(klient);
        msg_req.ma_alkohol = ma_alkohol;
        msg_req.wiek = klient->wiek;
        
        size_t msg_size = sizeof(Komunikat) - sizeof(long);
        
        ZapiszLogF(LOG_INFO, "Klient [ID: %d] dolacza do kolejki kas samoobslugowych (MSG IPC).", klient->id);
        
        if (msgsnd(msg_id, &msg_req, msg_size, 0) == -1) {
            if (errno != EINTR) perror("msgsnd samo");
            idzie_do_samoobslugowej = 0; //Blad wysylania -> sprobuj stacjonarnej
        } else {
            //Czekaj na odpowiedz (typ = 100 + ID)
            Komunikat msg_res;
            if (msgrcv(msg_id, &msg_res, msg_size, 100 + klient->id, 0) == -1) {
                if (errno == EINTR) { 
                    ZapiszLogF(LOG_OSTRZEZENIE, "Klient [ID: %d] przerywa oczekiwanie na kase samoobslugowa (powod: EWAKUACJA).", klient->id);
                    moze_kupic = 0; 
                    kod_bledu = 3;
                }
            } else {
                //Jest odpowiedz!
                int wynik = msg_res.id_klienta;
                int id_kasy_samo = msg_res.liczba_produktow;
                
                if (wynik == 0) {
                    WydrukujParagon(klient, "Kasa samoobslugowa", id_kasy_samo + 1);
                    ZapiszLogF(LOG_INFO, "Klient [ID: %d] obsluzony w kasie samoobslugowej %d.", klient->id, id_kasy_samo + 1);
                } else if (wynik == -2) {
                    moze_kupic = 0; 
                    kod_bledu = 1;
                    ZapiszLogF(LOG_BLAD, "Kasa samoobslugowa: Weryfikacja wieku nieudana!");
                } else if (wynik == -3) {
                    moze_kupic = 0; 
                    kod_bledu = 3;
                } else {
                    moze_kupic = 0; 
                    kod_bledu = 2;
                }
            }
        }
    }
    
    //=== KASA STACJONARNA ===
    if (moze_kupic && !idzie_do_samoobslugowej) {
        ZapiszLogF(LOG_INFO, "Klient [ID: %d] wybiera kase stacjonarna.", klient->id);
        
        //Sprawdz status obu kas stacjonarnych
        if (ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA) != 0) {
            moze_kupic = 0;
            kod_bledu = 2;
        } else {
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
                if (kasa1_otwarta && kolejka1 < kolejka2) {
                    wybrana_kasa = 0;
                } else {
                    wybrana_kasa = 1;
                }
            } else if (kasa1_otwarta) {
                wybrana_kasa = 0;
            } else if (kasa2_otwarta && kasa2_pelna) {
                wybrana_kasa = 0;
            } else if (kasa2_otwarta) {
                wybrana_kasa = 1;
            } else {
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
                
                Komunikat msg_req = {0};
                msg_req.mtype = wybrana_kasa + 1; 
                msg_req.id_klienta = klient->id;
                
                size_t msg_size = sizeof(Komunikat) - sizeof(long);
                
                if (ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA) == 0) {
                    stan_sklepu->kasy_stacjonarne[wybrana_kasa].liczba_w_kolejce++;
                    ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
                }
                
                ZapiszLogF(LOG_INFO, "Klient [ID: %d] dolacza do kolejki kasy stacjonarnej %d (MSG IPC).", klient->id, wybrana_kasa + 1);
                
                if (msgsnd(msg_id, &msg_req, msg_size, 0) == -1) {
                    //Jesli blad wysylania, cofnij inkrementacje
                    if (ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA) == 0) {
                        stan_sklepu->kasy_stacjonarne[wybrana_kasa].liczba_w_kolejce--;
                        ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
                    }
                    
                    if (errno == EINTR) {
                        kod_bledu = 3;
                    } else {
                        perror("Blad msgsnd");
                        kod_bledu = 2;
                    }
                    moze_kupic = 0;
                } else {
                    //BLOKUJACE czekanie na odpowiedz od kasjera
                    Komunikat msg_res;
                    if (msgrcv(msg_id, &msg_res, msg_size, 100 + klient->id, 0) == -1) {
                        if (errno == EINTR) {
                            ZapiszLogF(LOG_OSTRZEZENIE, "Klient [ID: %d] przerywa oczekiwanie na kase stacjonarna (powod: EWAKUACJA).", klient->id);
                            moze_kupic = 0;
                            kod_bledu = 3;
                        }
                    } else {
                        //Otrzymano potwierdzenie - jestem przy kasie!
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
                    }
                }
            }
        }
    }
    
    //=== WYJSCIE ZE SKLEPU ===
    klient->stan = STAN_WYJSCIE;
    
    //Sprawdz czy wychodzi podczas ewakuacji
    int ewakuacja_w_toku = CZY_KONCZYC(stan_sklepu);
    
    //Zaloguj wyjscie
    if (moze_kupic) {
        if (ewakuacja_w_toku) {
            ZapiszLogF(LOG_OSTRZEZENIE, "Klient [ID: %d] opuscil sklep (powod: EWAKUACJA).", klient->id);
        } else {
            ZapiszLogF(LOG_INFO, "Klient [ID: %d] opuscil sklep po dokonaniu zakupow.", klient->id);
        }
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
    
    //Zmniejsz licznik klientow
    if (ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA) == 0) {
        stan_sklepu->liczba_klientow_w_sklepie--;
        ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    }

    UsunKlienta(klient);
    OdlaczPamiecWspoldzielona(stan_sklepu);

    return kod_bledu;
}
#endif

