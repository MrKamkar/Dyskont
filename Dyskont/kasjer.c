#include "kasjer.h"
#include "semafory.h"
#include "logi.h"
#include <string.h>

//Tworzy nowego kasjera
Kasjer* StworzKasjera(int id_kasy) {
    if (id_kasy < 0 || id_kasy >= LICZBA_KAS_STACJONARNYCH) {
        return NULL;
    }
    
    Kasjer* kasjer = (Kasjer*)malloc(sizeof(Kasjer));
    if (!kasjer) return NULL;
    
    kasjer->id_kasy = id_kasy;
    kasjer->stan = KASJER_NIEAKTYWNY;
    kasjer->czas_ostatniej_aktywnosci = time(NULL);
    
    return kasjer;
}

//Usuwa kasjera
void UsunKasjera(Kasjer* kasjer) {
    if (kasjer) {
        free(kasjer);
    }
}

//Pobiera klienta z kolejki do kasy stacjonarnej
int PobierzKlientaZKolejki(int id_kasy, StanSklepu* stan, int sem_id) {
    if (!stan || id_kasy < 0 || id_kasy >= LICZBA_KAS_STACJONARNYCH) {
        return -1;
    }
    
    ZajmijSemafor(sem_id, MUTEX_KASY(id_kasy));
    
    KasaStacjonarna* kasa = &stan->kasy_stacjonarne[id_kasy];
    int id_klienta = -1;
    
    if (kasa->liczba_w_kolejce > 0) {
        id_klienta = kasa->kolejka[0];
        
        //Przesuniecie kolejki FIFO
        for (int i = 0; i < kasa->liczba_w_kolejce - 1; i++) {
            kasa->kolejka[i] = kasa->kolejka[i + 1];
        }
        kasa->liczba_w_kolejce--;
    }
    
    ZwolnijSemafor(sem_id, MUTEX_KASY(id_kasy));
    
    return id_klienta;
}

//Dodaje klienta do kolejki kasy stacjonarnej
int DodajDoKolejkiStacjonarnej(int id_kasy, int id_klienta, StanSklepu* stan, int sem_id) {
    if (!stan || id_kasy < 0 || id_kasy >= LICZBA_KAS_STACJONARNYCH) {
        return -1;
    }
    
    int sem_num = MUTEX_KASY(id_kasy);
    
    ZajmijSemafor(sem_id, sem_num);
    
    KasaStacjonarna* kasa = &stan->kasy_stacjonarne[id_kasy];
    int wynik = -1;
    
    if (kasa->stan == KASA_ZAMYKANA) {
        ZwolnijSemafor(sem_id, sem_num);
        return -1;
    }
    
    if (kasa->liczba_w_kolejce < MAX_KOLEJKA_STACJONARNA) {
        kasa->kolejka[kasa->liczba_w_kolejce] = id_klienta;
        kasa->liczba_w_kolejce++;
        wynik = 0;
    }
    
    ZwolnijSemafor(sem_id, sem_num);
    
    //Sygnal dla kasjera - jest klient w kolejce
    if (wynik == 0) {
        ZwolnijSemafor(sem_id, SEM_KLIENCI_KOLEJKA(id_kasy));
    }
    
    return wynik;
}

//Obsluga klienta przez kasjera
void ObsluzKlienta(Kasjer* kasjer, int id_klienta, int liczba_produktow, double suma, int tryb_testu) {
    if (!kasjer) return;
    
    char buf[256];
    sprintf(buf, "Kasjer [Kasa %d]: Rozpoczynam obsluge klienta [ID: %d], produktow: %d",
            kasjer->id_kasy + 1, id_klienta, liczba_produktow);
    ZapiszLog(LOG_INFO, buf);
    
    kasjer->stan = KASJER_OBSLUGUJE;
    
    //Symulacja skanowania (pomijana w trybie testu 1)
    if (tryb_testu == 0) {
        for (int i = 0; i < liczba_produktow; i++) {
            usleep(CZAS_OBSLUGI_PRODUKTU_MS * 1000);
        }
    }
    
    sprintf(buf, "Kasjer [Kasa %d]: Zakonczono obsluge klienta [ID: %d]. Suma: %.2f PLN",
            kasjer->id_kasy + 1, id_klienta, suma);
    ZapiszLog(LOG_INFO, buf);
    
    kasjer->czas_ostatniej_aktywnosci = time(NULL);
    kasjer->stan = KASJER_CZEKA_NA_KLIENTA;
}

//Sprawdza czy nalezy otworzyc kase 1
int CzyOtworzycKase1(StanSklepu* stan) {
    if (!stan) return 0;
    
    //Kasa 1 otwierana gdy > 3 osoby w kolejce do kas stacjonarnych
    return stan->kasy_stacjonarne[0].liczba_w_kolejce > 3;
}

//Punkt wejscia dla procesu kasjera
#ifdef KASJER_STANDALONE
int main(int argc, char* argv[]) {
    //Sprawdzenie argumentow przekazanych do programu
    if (argc != 2) {
        fprintf(stderr, "Uzycie: %s <id_kasy>\n", argv[0]);
        return 1;
    }
    
    int id_kasy = atoi(argv[1]);
    
    if (id_kasy < 0 || id_kasy >= LICZBA_KAS_STACJONARNYCH) {
        fprintf(stderr, "Nieprawidlowy ID kasy: %d (dozwolone: 0-%d)\n", 
                id_kasy, LICZBA_KAS_STACJONARNYCH - 1);
        return 1;
    }
    
    //Dolaczenie do pamieci wspoldzielonej
    StanSklepu* stan_sklepu = DolaczPamiecWspoldzielona();
    if (!stan_sklepu) {
        fprintf(stderr, "Kasjer [Kasa %d]: Nie mozna dolaczyc do pamieci wspoldzielonej\n", id_kasy + 1);
        return 1;
    }
    
    //Dolaczenie do semaforow
    int sem_id = DolaczSemafory();
    if (sem_id == -1) {
        fprintf(stderr, "Kasjer [Kasa %d]: Nie mozna dolaczyc do semaforow\n", id_kasy + 1);
        OdlaczPamiecWspoldzielona(stan_sklepu);
        return 1;
    }
    
    //Utworzenie kasjera
    Kasjer* kasjer = StworzKasjera(id_kasy);
    if (!kasjer) {
        fprintf(stderr, "Kasjer [Kasa %d]: Nie udalo sie utworzyc kasjera\n", id_kasy + 1);
        OdlaczPamiecWspoldzielona(stan_sklepu);
        return 1;
    }
    
    //Inicjalizacja systemu logowania (uzywa globalnej sciezki IPC_SCIEZKA)
    InicjalizujSystemLogowania();
    
    char buf[256];
    sprintf(buf, "Kasjer [Kasa %d]: Proces uruchomiony, oczekuje na otwarcie kasy.", id_kasy + 1);
    ZapiszLog(LOG_INFO, buf);
    
    //Glowna petla kasjera
    while (1) {
        //Sprawdzenie flagi ewakuacji
        if (stan_sklepu->flaga_ewakuacji) {
            sprintf(buf, "Kasjer [Kasa %d]: Otrzymano sygnal ewakuacji, koncze prace.", id_kasy + 1);
            ZapiszLog(LOG_INFO, buf);
            break;
        }
        
        //Sprawdzenie stanu kasy i polecen
        ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
        StanKasy stan_kasy = stan_sklepu->kasy_stacjonarne[id_kasy].stan;
        int w_kolejce = stan_sklepu->kasy_stacjonarne[id_kasy].liczba_w_kolejce;
        int polecenie = stan_sklepu->polecenie_kierownika;
        int kasa_do_zamkniecia = stan_sklepu->id_kasy_do_zamkniecia;
        
        //Reakcja na polecenie zamkniecia - ustaw ZAMYKANA jesli sa klienci
        if (polecenie == 2 && kasa_do_zamkniecia == id_kasy && stan_kasy != KASA_ZAMKNIETA && stan_kasy != KASA_ZAMYKANA) {
            if (w_kolejce > 0) {
                stan_sklepu->kasy_stacjonarne[id_kasy].stan = KASA_ZAMYKANA;
                stan_kasy = KASA_ZAMYKANA;
                sprintf(buf, "Kasjer [Kasa %d]: Polecenie zamkniecia - przechodze w tryb ZAMYKANA (kolejka: %d).", id_kasy + 1, w_kolejce);
                ZapiszLog(LOG_INFO, buf);
                
                //Wyczysc polecenie bo juz zareagowalismy (stan ZAMYKANA wystarczy)
                stan_sklepu->polecenie_kierownika = 0;
                stan_sklepu->id_kasy_do_zamkniecia = -1;
            }
        }

        
        //Automatyczne otwieranie kasy gdy w kolejce >= 3 osoby (tylko kasa 1)
        if (id_kasy == 0 && stan_kasy == KASA_ZAMKNIETA && w_kolejce >= 3) {
            stan_sklepu->kasy_stacjonarne[id_kasy].stan = KASA_WOLNA;
            stan_sklepu->kasy_stacjonarne[id_kasy].czas_ostatniej_obslugi = time(NULL);
            stan_kasy = KASA_WOLNA;
            ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
            
            sprintf(buf, "Kasjer [Kasa %d]: Automatyczne otwarcie kasy (>= 3 osoby w kolejce).", id_kasy + 1);
            ZapiszLog(LOG_INFO, buf);
        } else {
            ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
        }
        
        if (stan_kasy == KASA_ZAMKNIETA) {
            kasjer->stan = KASJER_NIEAKTYWNY;
            
            //Blokujace czekanie na sygnal otwarcia kasy (max 2 sek)
            struct timespec timeout = {2, 0};
            struct sembuf op = {SEM_OTWORZ_KASA_STACJ(id_kasy), -1, 0};
            semtimedop(sem_id, &op, 1, &timeout);  //Czeka na sygnal lub timeout
            continue;
        }
        
        kasjer->stan = KASJER_CZEKA_NA_KLIENTA;
        
        //Blokujace czekanie na klienta w kolejce (max 1 sek dla sprawdzenia ewakuacji/zamkniecia)
        struct timespec timeout = {1, 0};
        struct sembuf op = {SEM_KLIENCI_KOLEJKA(id_kasy), -1, 0};
        
        if (semtimedop(sem_id, &op, 1, &timeout) == 0) {
            //Jest klient w kolejce
            int id_klienta = PobierzKlientaZKolejki(id_kasy, stan_sklepu, sem_id);
            
            if (id_klienta != -1) {
                int liczba_produktow = 3 + (rand() % 8);
                double suma = liczba_produktow * 10.0;
                
                ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
                //Jesli jest ZAMYKANA, nie zmieniaj na ZAJETA (zeby klienci wiedzieli ze nie mozna dolaczac)
                if (stan_sklepu->kasy_stacjonarne[id_kasy].stan != KASA_ZAMYKANA) {
                    stan_sklepu->kasy_stacjonarne[id_kasy].stan = KASA_ZAJETA;
                }
                stan_sklepu->kasy_stacjonarne[id_kasy].id_klienta = id_klienta;
                ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
                
                ObsluzKlienta(kasjer, id_klienta, liczba_produktow, suma, stan_sklepu->tryb_testu);
                
                ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
                //Jesli jest ZAMYKANA, nie zmieniaj na WOLNA (pozostan w ZAMYKANA az do oproznienia kolejki)
                if (stan_sklepu->kasy_stacjonarne[id_kasy].stan != KASA_ZAMYKANA) {
                    stan_sklepu->kasy_stacjonarne[id_kasy].stan = KASA_WOLNA;
                }
                stan_sklepu->kasy_stacjonarne[id_kasy].id_klienta = -1;
                stan_sklepu->kasy_stacjonarne[id_kasy].czas_ostatniej_obslugi = time(NULL);
                ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
            }
        } else {
            //Timeout (brak sygnalu na semaforze przez 1 sekunde)
            
            //Sprawdz stan kolejki - jesli sa ludzie, a semafor timeoutowal, to mamy desynchronizacje!
            ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
            KasaStacjonarna* kasa = &stan_sklepu->kasy_stacjonarne[id_kasy];
            int w_kolejce_check = kasa->liczba_w_kolejce;
            ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
            
            if (w_kolejce_check > 0) {
                //Awaryjna obsluga - semafor "zgubil" klientow
                //ZapiszLog(LOG_DEBUG, "Kasjer [Kasa %d]: Obsluga w trybie polling (semafor=0, kolejka>0).");
                
                int id_klienta = PobierzKlientaZKolejki(id_kasy, stan_sklepu, sem_id);
                if (id_klienta != -1) {
                    int liczba_produktow = 3 + (rand() % 8);
                    double suma = liczba_produktow * 10.0;
                    
                    ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
                    //Jesli jest ZAMYKANA, nie zmieniaj na ZAJETA
                    if (stan_sklepu->kasy_stacjonarne[id_kasy].stan != KASA_ZAMYKANA) {
                        stan_sklepu->kasy_stacjonarne[id_kasy].stan = KASA_ZAJETA;
                    }
                    stan_sklepu->kasy_stacjonarne[id_kasy].id_klienta = id_klienta;
                    ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
                    
                    ObsluzKlienta(kasjer, id_klienta, liczba_produktow, suma, stan_sklepu->tryb_testu);
                    
                    ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
                    //Jesli jest ZAMYKANA, nie zmieniaj na WOLNA
                    if (stan_sklepu->kasy_stacjonarne[id_kasy].stan != KASA_ZAMYKANA) {
                        stan_sklepu->kasy_stacjonarne[id_kasy].stan = KASA_WOLNA;
                    }
                    stan_sklepu->kasy_stacjonarne[id_kasy].id_klienta = -1;
                    stan_sklepu->kasy_stacjonarne[id_kasy].czas_ostatniej_obslugi = time(NULL);
                    ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
                }
                continue; //Wroc do petli
            }

            //Standardowa logika zamykania (gdy kolejka pusta)
            time_t teraz = time(NULL);
            
            ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
            kasa = &stan_sklepu->kasy_stacjonarne[id_kasy];
            time_t ostatnia_obsluga = kasa->czas_ostatniej_obslugi;
            int w_kolejce = kasa->liczba_w_kolejce;
            int polecenie = stan_sklepu->polecenie_kierownika;
            int kasa_do_zamkniecia = stan_sklepu->id_kasy_do_zamkniecia;
            StanKasy aktualny_stan = kasa->stan;
            ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
            
            if ((aktualny_stan == KASA_ZAMYKANA || (polecenie == 2 && kasa_do_zamkniecia == id_kasy)) && w_kolejce == 0) {
                sprintf(buf, "Kasjer [Kasa %d]: Polecenie kierownika - zamykam kase.", id_kasy + 1);
                ZapiszLog(LOG_INFO, buf);
                
                ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
                stan_sklepu->kasy_stacjonarne[id_kasy].stan = KASA_ZAMKNIETA;
                stan_sklepu->polecenie_kierownika = 0;  //Wyczysc polecenie
                stan_sklepu->id_kasy_do_zamkniecia = -1;
                ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
                
                kasjer->stan = KASJER_ZAMYKA_KASE;
            }
            //30 sekund bez klienta, zamkniecie kasy
            else if (ostatnia_obsluga > 0 && w_kolejce == 0 && 
                (teraz - ostatnia_obsluga) >= CZAS_BEZCZYNNOSCI_DO_ZAMKNIECIA) {
                
                sprintf(buf, "Kasjer [Kasa %d]: Brak klientow przez %d sekund, zamykam kase.",
                        id_kasy + 1, CZAS_BEZCZYNNOSCI_DO_ZAMKNIECIA);
                ZapiszLog(LOG_INFO, buf);
                
                ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
                stan_sklepu->kasy_stacjonarne[id_kasy].stan = KASA_ZAMKNIETA;
                ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
                
                kasjer->stan = KASJER_ZAMYKA_KASE;
            }
        }
    }
    
    //Czyszczenie
    UsunKasjera(kasjer);
    OdlaczPamiecWspoldzielona(stan_sklepu);
    
    return 0;
}
#endif
