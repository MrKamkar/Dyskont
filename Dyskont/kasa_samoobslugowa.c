#include "kasa_samoobslugowa.h"
#include "pracownik_obslugi.h"
#include "semafory.h"
#include "logi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//Dodaje klienta do wspolnej kolejki samoobslugowej
int DodajDoKolejkiSamoobslugowej(int id_klienta, StanSklepu* stan, int sem_id) {
    if (!stan) return -1;
    
    ZajmijSemafor(sem_id, SEM_KOLEJKA_SAMO);
    
    int wynik = -1;
    if (stan->liczba_w_kolejce_samo < MAX_KOLEJKA_SAMO) {
        stan->kolejka_samo[stan->liczba_w_kolejce_samo] = id_klienta;
        stan->liczba_w_kolejce_samo++;
        wynik = 0;
    }
    
    ZwolnijSemafor(sem_id, SEM_KOLEJKA_SAMO);
    return wynik;
}

//Pobiera pierwszego klienta z kolejki (FIFO)
int PobierzZKolejkiSamoobslugowej(StanSklepu* stan, int sem_id) {
    if (!stan) return -1;
    
    ZajmijSemafor(sem_id, SEM_KOLEJKA_SAMO);
    
    int id_klienta = -1;
    if (stan->liczba_w_kolejce_samo > 0) {
        id_klienta = stan->kolejka_samo[0];
        
        //Przesuniecie kolejki FIFO
        for (int i = 0; i < stan->liczba_w_kolejce_samo - 1; i++) {
            stan->kolejka_samo[i] = stan->kolejka_samo[i + 1];
        }
        stan->liczba_w_kolejce_samo--;
    }
    
    ZwolnijSemafor(sem_id, SEM_KOLEJKA_SAMO);
    return id_klienta;
}

//Szuka wolnej kasy samoobslugowej, zwraca jej indeks lub -1
int ZnajdzWolnaKase(StanSklepu* stan, int sem_id) {
    if (!stan) return -1;
    
    ZajmijSemafor(sem_id, SEM_PAMIEC_WSPOLDZIELONA);
    
    int wolna_kasa = -1;
    for (int i = 0; i < LICZBA_KAS_SAMOOBSLUGOWYCH; i++) {
        if (stan->kasy_samo[i].stan == KASA_WOLNA) {
            wolna_kasa = i;
            break;
        }
    }
    
    ZwolnijSemafor(sem_id, SEM_PAMIEC_WSPOLDZIELONA);
    return wolna_kasa;
}

//Zajmuje kase dla klienta
int ZajmijKase(int id_kasy, int id_klienta, StanSklepu* stan, int sem_id) {
    if (!stan || id_kasy < 0 || id_kasy >= LICZBA_KAS_SAMOOBSLUGOWYCH) return -1;
    
    ZajmijSemafor(sem_id, SEM_PAMIEC_WSPOLDZIELONA);
    
    int wynik = -1;
    if (stan->kasy_samo[id_kasy].stan == KASA_WOLNA) {
        stan->kasy_samo[id_kasy].stan = KASA_ZAJETA;
        stan->kasy_samo[id_kasy].id_klienta = id_klienta;
        stan->kasy_samo[id_kasy].czas_rozpoczecia = time(NULL);
        wynik = 0;
    }
    
    ZwolnijSemafor(sem_id, SEM_PAMIEC_WSPOLDZIELONA);
    return wynik;
}

//Zwalnia kase
void ZwolnijKase(int id_kasy, StanSklepu* stan, int sem_id) {
    if (!stan || id_kasy < 0 || id_kasy >= LICZBA_KAS_SAMOOBSLUGOWYCH) return;
    
    ZajmijSemafor(sem_id, SEM_PAMIEC_WSPOLDZIELONA);
    stan->kasy_samo[id_kasy].stan = KASA_WOLNA;
    stan->kasy_samo[id_kasy].id_klienta = -1;
    stan->kasy_samo[id_kasy].czas_rozpoczecia = 0;
    ZwolnijSemafor(sem_id, SEM_PAMIEC_WSPOLDZIELONA);
}

//Oblicza wymagana liczbe kas: K klientow = 1 kasa, min 3
int ObliczWymaganaLiczbeKas(int liczba_klientow) {
    int wymagane = (liczba_klientow + KLIENCI_NA_KASE - 1) / KLIENCI_NA_KASE;
    if (wymagane < MIN_KAS_SAMO_CZYNNYCH) wymagane = MIN_KAS_SAMO_CZYNNYCH;
    if (wymagane > LICZBA_KAS_SAMOOBSLUGOWYCH) wymagane = LICZBA_KAS_SAMOOBSLUGOWYCH;
    return wymagane;
}

//Aktualizuje liczbe czynnych kas w zaleznosci od liczby klientow
void AktualizujLiczbeKas(StanSklepu* stan, int sem_id) {
    if (!stan) return;
    
    ZajmijSemafor(sem_id, SEM_PAMIEC_WSPOLDZIELONA);
    
    int liczba_klientow = stan->liczba_klientow_w_sklepie;
    int wymagane = ObliczWymaganaLiczbeKas(liczba_klientow);
    int aktualne = stan->liczba_czynnych_kas_samo;
    
    char buf[256];
    
    //Otworz wiecej kas jesli potrzeba
    if (wymagane > aktualne) {
        for (int i = 0; i < LICZBA_KAS_SAMOOBSLUGOWYCH && aktualne < wymagane; i++) {
            if (stan->kasy_samo[i].stan == KASA_ZAMKNIETA) {
                stan->kasy_samo[i].stan = KASA_WOLNA;
                stan->kasy_samo[i].id_klienta = -1;
                aktualne++;
                sprintf(buf, "Kasa samoobslugowa [%d]: Otwarta (wymagane: %d kas dla %d klientow)", 
                        i + 1, wymagane, liczba_klientow);
                ZapiszLog(LOG_INFO, buf);
            }
        }
        stan->liczba_czynnych_kas_samo = aktualne;
    }
    //Zamknij nadmiarowe kasy (tylko wolne)
    else if (wymagane < aktualne && aktualne > MIN_KAS_SAMO_CZYNNYCH) {
        for (int i = LICZBA_KAS_SAMOOBSLUGOWYCH - 1; i >= 0 && aktualne > wymagane && aktualne > MIN_KAS_SAMO_CZYNNYCH; i--) {
            if (stan->kasy_samo[i].stan == KASA_WOLNA) {
                stan->kasy_samo[i].stan = KASA_ZAMKNIETA;
                aktualne--;
                sprintf(buf, "Kasa samoobslugowa [%d]: Zamknieta (wymagane: %d kas dla %d klientow)", 
                        i + 1, wymagane, liczba_klientow);
                ZapiszLog(LOG_INFO, buf);
            }
        }
        stan->liczba_czynnych_kas_samo = aktualne;
    }
    
    ZwolnijSemafor(sem_id, SEM_PAMIEC_WSPOLDZIELONA);
}

//Obsluga klienta przy kasie samoobslugowej (zwraca 0=sukces, -1=timeout, -2=niepelnoletni)
int ObsluzKlientaSamoobslugowo(int id_kasy, int id_klienta, int liczba_produktow, 
                                 double suma, int ma_alkohol, int wiek, StanSklepu* stan, int sem_id) {
    char buf[256];
    
    sprintf(buf, "Kasa samoobslugowa [%d]: Klient [ID: %d] rozpoczyna skanowanie %d produktow",
            id_kasy + 1, id_klienta, liczba_produktow);
    ZapiszLog(LOG_INFO, buf);
    
    //Skanowanie produktow
    for (int i = 0; i < liczba_produktow; i++) {
        usleep(CZAS_SKANOWANIA_PRODUKTU_MS * 1000);
        
        //Losowa blokada kasy
        if (rand() % SZANSA_BLOKADY == 0) {
            sprintf(buf, "Kasa samoobslugowa [%d]: BLOKADA! Niezgodnosc wagi produktu.",
                    id_kasy + 1);
            ZapiszLog(LOG_OSTRZEZENIE, buf);
            
            ZajmijSemafor(sem_id, SEM_PAMIEC_WSPOLDZIELONA);
            stan->kasy_samo[id_kasy].stan = KASA_ZABLOKOWANA;
            ZwolnijSemafor(sem_id, SEM_PAMIEC_WSPOLDZIELONA);
            
            //Wyslanie zadania do pracownika obslugi przez FIFO
            ZadanieObslugi zadanie;
            zadanie.typ = ZADANIE_ODBLOKUJ_KASE;
            zadanie.id_kasy = id_kasy;
            zadanie.id_klienta = id_klienta;
            zadanie.wiek_klienta = wiek;
            WyslijZadanieObslugi(&zadanie);
            
            //Czekaj az pracownik odblokuje kase
            time_t czas_blokady = time(NULL);
            int timeout_blokady = 0;
            while (1) {
                //Sprawdz timeout
                if (time(NULL) - czas_blokady >= MAX_CZAS_OCZEKIWANIA) {
                    sprintf(buf, "Kasa samoobslugowa [%d]: Timeout! Pracownik nie podszedl w ciagu %d sek.",
                            id_kasy + 1, MAX_CZAS_OCZEKIWANIA);
                    ZapiszLog(LOG_BLAD, buf);
                    timeout_blokady = 1;
                    break;
                }
                
                ZajmijSemafor(sem_id, SEM_PAMIEC_WSPOLDZIELONA);
                StanKasy aktualny_stan = stan->kasy_samo[id_kasy].stan;
                ZwolnijSemafor(sem_id, SEM_PAMIEC_WSPOLDZIELONA);
                
                if (aktualny_stan != KASA_ZABLOKOWANA) {
                    break;
                }
                usleep(100000); //100ms
            }
            
            //Jesli timeout, klient moze isc do stacjonarnej
            if (timeout_blokady) {
                ZwolnijKase(id_kasy, stan, sem_id);
                return -1;  //Timeout - klient powinien isc do kasy stacjonarnej
            }
            
            sprintf(buf, "Kasa samoobslugowa [%d]: Odblokowana przez pracownika obslugi.", id_kasy + 1);
            ZapiszLog(LOG_INFO, buf);
        }
    }
    
    //Weryfikacja wieku przy alkoholu
    if (ma_alkohol) {
        sprintf(buf, "Kasa samoobslugowa [%d]: Alkohol wykryty! Wzywam pracownika...",
                id_kasy + 1);
        ZapiszLog(LOG_INFO, buf);
        
        //Wyslanie zadania weryfikacji wieku przez FIFO
        ZadanieObslugi zadanie;
        zadanie.typ = ZADANIE_WERYFIKUJ_WIEK;
        zadanie.id_kasy = id_kasy;
        zadanie.id_klienta = id_klienta;
        zadanie.wiek_klienta = wiek;
        WyslijZadanieObslugi(&zadanie);
        
        //Krotkie oczekiwanie na weryfikacje
        usleep(500000); //500ms
        
        if (wiek < 18) {
            sprintf(buf, "Kasa samoobslugowa [%d]: ODMOWA! Klient [ID: %d] niepelnoletni (wiek: %d)",
                    id_kasy + 1, id_klienta, wiek);
            ZapiszLog(LOG_BLAD, buf);
            return -2;  //Niepelnoletni
        } else {
            sprintf(buf, "Kasa samoobslugowa [%d]: Weryfikacja wieku OK (wiek: %d)", id_kasy + 1, wiek);
            ZapiszLog(LOG_INFO, buf);
        }
    }
    
    //Platnosc karta i wydruk paragonu
    sprintf(buf, "Kasa samoobslugowa [%d]: Klient [ID: %d] zaplacil karta. Suma: %.2f PLN. Paragon wydrukowany.",
            id_kasy + 1, id_klienta, suma);
    ZapiszLog(LOG_INFO, buf);
    
    return 0;  //Sukces
}

//Glowna funkcja procesu kasy samoobslugowej
#ifdef KASA_SAMO_STANDALONE
int main(int argc, char* argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Uzycie: %s <sciezka> <id_kasy>\n", argv[0]);
        return 1;
    }
    
    const char* sciezka = argv[1];
    int id_kasy = atoi(argv[2]);
    
    if (id_kasy < 0 || id_kasy >= LICZBA_KAS_SAMOOBSLUGOWYCH) {
        fprintf(stderr, "Nieprawidlowy ID kasy: %d (dozwolone: 0-%d)\n", 
                id_kasy, LICZBA_KAS_SAMOOBSLUGOWYCH - 1);
        return 1;
    }
    
    //Dolaczenie do pamieci wspoldzielonej
    StanSklepu* stan_sklepu = DolaczPamiecWspoldzielona(sciezka);
    if (!stan_sklepu) {
        fprintf(stderr, "Kasa samoobslugowa [%d]: Nie mozna dolaczyc do pamieci wspoldzielonej\n", id_kasy + 1);
        return 1;
    }
    
    //Dolaczenie do semaforow
    int sem_id = DolaczSemafory(sciezka);
    if (sem_id == -1) {
        fprintf(stderr, "Kasa samoobslugowa [%d]: Nie mozna dolaczyc do semaforow\n", id_kasy + 1);
        OdlaczPamiecWspoldzielona(stan_sklepu);
        return 1;
    }
    
    //Inicjalizacja systemu logowania
    InicjalizujSystemLogowania(sciezka);
    
    srand(time(NULL) ^ getpid());
    
    char buf[256];
    sprintf(buf, "Kasa samoobslugowa [%d]: Proces uruchomiony.", id_kasy + 1);
    ZapiszLog(LOG_INFO, buf);
    
    //Glowna petla
    while (1) {
        //Sprawdzenie flagi ewakuacji
        if (stan_sklepu->flaga_ewakuacji) {
            sprintf(buf, "Kasa samoobslugowa [%d]: Ewakuacja - koncze prace.", id_kasy + 1);
            ZapiszLog(LOG_INFO, buf);
            break;
        }
        
        //Sprawdzenie stanu kasy
        ZajmijSemafor(sem_id, SEM_PAMIEC_WSPOLDZIELONA);
        StanKasy stan_kasy = stan_sklepu->kasy_samo[id_kasy].stan;
        ZwolnijSemafor(sem_id, SEM_PAMIEC_WSPOLDZIELONA);
        
        if (stan_kasy == KASA_ZAMKNIETA) {
            usleep(500000); //500ms
            continue;
        }
        
        //Kasa jest wolna - mozna obsluzyc klienta z kolejki
        if (stan_kasy == KASA_WOLNA) {
            //Aktualizacja liczby kas
            AktualizujLiczbeKas(stan_sklepu, sem_id);
            usleep(100000); //100ms
        }
        
        usleep(100000); //100ms
    }
    
    OdlaczPamiecWspoldzielona(stan_sklepu);
    return 0;
}
#endif
