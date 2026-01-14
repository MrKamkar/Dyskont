#include "kasjer.h"
#include "semafory.h"
#include "logi.h"
#include <stdio.h>
#include <stdlib.h>
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
    
    //Wybor odpowiedniego semafora
    int sem_num = (id_kasy == 0) ? SEM_KASA_STACJONARNA_1 : SEM_KASA_STACJONARNA_2;
    
    ZajmijSemafor(sem_id, sem_num);
    
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
    
    ZwolnijSemafor(sem_id, sem_num);
    
    return id_klienta;
}

//Dodaje klienta do kolejki kasy stacjonarnej
int DodajDoKolejkiStacjonarnej(int id_kasy, int id_klienta, StanSklepu* stan, int sem_id) {
    if (!stan || id_kasy < 0 || id_kasy >= LICZBA_KAS_STACJONARNYCH) {
        return -1;
    }
    
    int sem_num = (id_kasy == 0) ? SEM_KASA_STACJONARNA_1 : SEM_KASA_STACJONARNA_2;
    
    ZajmijSemafor(sem_id, sem_num);
    
    KasaStacjonarna* kasa = &stan->kasy_stacjonarne[id_kasy];
    int wynik = -1;
    
    if (kasa->stan == KASA_ZAMYKANA) {
        ZwolnijSemafor(sem_id, sem_num);
        return -1;
    }
    if (id_kasy == 1 && kasa->stan == KASA_ZAMKNIETA) {
        //Kasa 2 musi byc otwarta przez kierownika
        ZwolnijSemafor(sem_id, sem_num);
        return -1;
    }
    
    if (kasa->liczba_w_kolejce < MAX_KOLEJKA_STACJONARNA) {
        kasa->kolejka[kasa->liczba_w_kolejce] = id_klienta;
        kasa->liczba_w_kolejce++;
        wynik = 0;
    }
    
    ZwolnijSemafor(sem_id, sem_num);
    
    return wynik;
}

//Obsluga klienta przez kasjera
void ObsluzKlienta(Kasjer* kasjer, int id_klienta, int liczba_produktow, double suma) {
    if (!kasjer) return;
    
    char buf[256];
    sprintf(buf, "Kasjer [Kasa %d]: Rozpoczynam obsluge klienta [ID: %d], produktow: %d",
            kasjer->id_kasy + 1, id_klienta, liczba_produktow);
    ZapiszLog(LOG_INFO, buf);
    
    kasjer->stan = KASJER_OBSLUGUJE;
    
    for (int i = 0; i < liczba_produktow; i++) {
        usleep(CZAS_OBSLUGI_PRODUKTU_MS * 1000);
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
    if (argc != 3) {
        fprintf(stderr, "Uzycie: %s <sciezka> <id_kasy>\n", argv[0]);
        return 1;
    }
    
    const char* sciezka = argv[1];
    int id_kasy = atoi(argv[2]);
    
    if (id_kasy < 0 || id_kasy >= LICZBA_KAS_STACJONARNYCH) {
        fprintf(stderr, "Nieprawidlowy ID kasy: %d (dozwolone: 0-%d)\n", 
                id_kasy, LICZBA_KAS_STACJONARNYCH - 1);
        return 1;
    }
    
    //Dolaczenie do pamieci wspoldzielonej
    StanSklepu* stan_sklepu = DolaczPamiecWspoldzielona(sciezka);
    if (!stan_sklepu) {
        fprintf(stderr, "Kasjer [Kasa %d]: Nie mozna dolaczyc do pamieci wspoldzielonej\n", id_kasy + 1);
        return 1;
    }
    
    //Dolaczenie do semaforow
    int sem_id = DolaczSemafory(sciezka);
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
    
    //Inicjalizacja systemu logowania
    InicjalizujSystemLogowania(sciezka);
    
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
        
        //Sprawdzenie stanu kasy
        ZajmijSemafor(sem_id, SEM_PAMIEC_WSPOLDZIELONA);
        StanKasy stan_kasy = stan_sklepu->kasy_stacjonarne[id_kasy].stan;
        ZwolnijSemafor(sem_id, SEM_PAMIEC_WSPOLDZIELONA);
        
        if (stan_kasy == KASA_ZAMKNIETA) {
            kasjer->stan = KASJER_NIEAKTYWNY;
            usleep(500000); //500ms
            continue;
        }
        
        kasjer->stan = KASJER_CZEKA_NA_KLIENTA;
        int id_klienta = PobierzKlientaZKolejki(id_kasy, stan_sklepu, sem_id);
        
        if (id_klienta != -1) {
            int liczba_produktow = 3 + (rand() % 8);
            double suma = liczba_produktow * 10.0;
            
            ZajmijSemafor(sem_id, SEM_PAMIEC_WSPOLDZIELONA);
            stan_sklepu->kasy_stacjonarne[id_kasy].stan = KASA_ZAJETA;
            stan_sklepu->kasy_stacjonarne[id_kasy].id_klienta = id_klienta;
            ZwolnijSemafor(sem_id, SEM_PAMIEC_WSPOLDZIELONA);
            
            ObsluzKlienta(kasjer, id_klienta, liczba_produktow, suma);
            
            ZajmijSemafor(sem_id, SEM_PAMIEC_WSPOLDZIELONA);
            stan_sklepu->kasy_stacjonarne[id_kasy].stan = KASA_WOLNA;
            stan_sklepu->kasy_stacjonarne[id_kasy].id_klienta = -1;
            stan_sklepu->kasy_stacjonarne[id_kasy].czas_ostatniej_obslugi = time(NULL);
            ZwolnijSemafor(sem_id, SEM_PAMIEC_WSPOLDZIELONA);
        } else {
            //Sprawdzenie czy kasa jest bezczynna

            time_t teraz = time(NULL);
            
            ZajmijSemafor(sem_id, SEM_PAMIEC_WSPOLDZIELONA);
            time_t ostatnia_obsluga = stan_sklepu->kasy_stacjonarne[id_kasy].czas_ostatniej_obslugi;
            int w_kolejce = stan_sklepu->kasy_stacjonarne[id_kasy].liczba_w_kolejce;
            int polecenie = stan_sklepu->polecenie_kierownika;
            int kasa_do_zamkniecia = stan_sklepu->id_kasy_do_zamkniecia;
            ZwolnijSemafor(sem_id, SEM_PAMIEC_WSPOLDZIELONA);
            
            //Sprawdz czy kasa jest w stanie ZAMYKANA i kolejka pusta
            ZajmijSemafor(sem_id, SEM_PAMIEC_WSPOLDZIELONA);
            StanKasy aktualny_stan = stan_sklepu->kasy_stacjonarne[id_kasy].stan;
            ZwolnijSemafor(sem_id, SEM_PAMIEC_WSPOLDZIELONA);
            
            if ((aktualny_stan == KASA_ZAMYKANA || (polecenie == 2 && kasa_do_zamkniecia == id_kasy)) && w_kolejce == 0) {
                sprintf(buf, "Kasjer [Kasa %d]: Polecenie kierownika - zamykam kase.", id_kasy + 1);
                ZapiszLog(LOG_INFO, buf);
                
                ZajmijSemafor(sem_id, SEM_PAMIEC_WSPOLDZIELONA);
                stan_sklepu->kasy_stacjonarne[id_kasy].stan = KASA_ZAMKNIETA;
                stan_sklepu->polecenie_kierownika = 0;  //Wyczysc polecenie
                stan_sklepu->id_kasy_do_zamkniecia = -1;
                ZwolnijSemafor(sem_id, SEM_PAMIEC_WSPOLDZIELONA);
                
                kasjer->stan = KASJER_ZAMYKA_KASE;
            }
            //30 sekund bez klienta, zamkniecie kasy
            else if (ostatnia_obsluga > 0 && w_kolejce == 0 && 
                (teraz - ostatnia_obsluga) >= CZAS_BEZCZYNNOSCI_DO_ZAMKNIECIA) {
                
                sprintf(buf, "Kasjer [Kasa %d]: Brak klientow przez %d sekund, zamykam kase.",
                        id_kasy + 1, CZAS_BEZCZYNNOSCI_DO_ZAMKNIECIA);
                ZapiszLog(LOG_INFO, buf);
                
                ZajmijSemafor(sem_id, SEM_PAMIEC_WSPOLDZIELONA);
                stan_sklepu->kasy_stacjonarne[id_kasy].stan = KASA_ZAMKNIETA;
                ZwolnijSemafor(sem_id, SEM_PAMIEC_WSPOLDZIELONA);
                
                kasjer->stan = KASJER_ZAMYKA_KASE;
            }
            
            usleep(200000);
        }
    }
    
    //Czyszczenie
    UsunKasjera(kasjer);
    OdlaczPamiecWspoldzielona(stan_sklepu);
    
    return 0;
}
#endif
