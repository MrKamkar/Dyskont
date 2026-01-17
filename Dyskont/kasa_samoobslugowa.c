#include "kasa_samoobslugowa.h"
#include "pracownik_obslugi.h"
#include "wspolne.h"
#include <string.h>
#include <signal.h>

//Zajmuje kase dla klienta
int ZajmijKase(int id_kasy, int id_klienta, StanSklepu* stan, int sem_id) {
    if (!stan || id_kasy < 0 || id_kasy >= LICZBA_KAS_SAMOOBSLUGOWYCH) return -1;
    
    if (ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA) != 0) return -1;
    
    int wynik = -1;
    if (stan->kasy_samo[id_kasy].stan == KASA_WOLNA) {
        stan->kasy_samo[id_kasy].stan = KASA_ZAJETA;
        stan->kasy_samo[id_kasy].id_klienta = id_klienta;
        stan->kasy_samo[id_kasy].czas_rozpoczecia = time(NULL);
        wynik = 0;
    }
    
    ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    return wynik;
}

//Zwalnia kase
void ZwolnijKase(int id_kasy, StanSklepu* stan, int sem_id) {
    if (!stan || id_kasy < 0 || id_kasy >= LICZBA_KAS_SAMOOBSLUGOWYCH) return;
    
    if (ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA) != 0) return;
    stan->kasy_samo[id_kasy].stan = KASA_WOLNA;
    stan->kasy_samo[id_kasy].id_klienta = -1;
    stan->kasy_samo[id_kasy].czas_rozpoczecia = 0;
    ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
}

//Oblicza wymagana liczbe kas: K klientow = 1 kasa, min 3
int ObliczWymaganaLiczbeKas(int liczba_klientow) {
    int wymagane = (liczba_klientow + KLIENCI_NA_KASE - 1) / KLIENCI_NA_KASE;
    if (wymagane < MIN_KAS_SAMO_CZYNNYCH) wymagane = MIN_KAS_SAMO_CZYNNYCH;
    if (wymagane > LICZBA_KAS_SAMOOBSLUGOWYCH) wymagane = LICZBA_KAS_SAMOOBSLUGOWYCH;
    return wymagane;
}

//Obsluga klienta przy kasie samoobslugowej
int ObsluzKlientaSamoobslugowo(int id_kasy, int id_klienta, int liczba_produktow, 
                                 double suma, int ma_alkohol, int wiek, StanSklepu* stan, int sem_id) {
    ZapiszLogF(LOG_INFO, "Kasa samoobslugowa [%d]: Klient [ID: %d] rozpoczyna skanowanie %d produktow",
            id_kasy + 1, id_klienta, liczba_produktow);
    
    //Skanowanie produktow
    for (int i = 0; i < liczba_produktow; i++) {
        //Symulacja skanowania (pomijana w trybie testu 1)
        SYMULACJA_USLEEP(stan, CZAS_SKANOWANIA_PRODUKTU_MS * 1000);
        
        //Losowa blokada kasy
        if (rand() % SZANSA_BLOKADY == 0) {
            ZapiszLogF(LOG_OSTRZEZENIE, "Kasa samoobslugowa [%d]: BLOKADA! Niezgodnosc wagi produktu.",
                    id_kasy + 1);
            
            if (ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA) != 0) return -3;
            stan->kasy_samo[id_kasy].stan = KASA_ZABLOKOWANA;
            ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
            
            //Wyslanie zadania do pracownika obslugi (MSGQ)
            //Odblokowanie
            if (WyslijZadanieObslugi(id_kasy, OP_ODBLOKOWANIE_KASY, 0) == 1) {
                // Pracownik odblokowal
                if (ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA) == 0) {
                     stan->kasy_samo[id_kasy].stan = KASA_ZAJETA;
                     ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
                }
                ZapiszLogF(LOG_INFO, "Kasa samoobslugowa [%d]: Odblokowana przez pracownika obslugi.", id_kasy + 1);
            } else {
                //Timeout lub blad
                ZapiszLogF(LOG_BLAD, "Kasa samoobslugowa [%d]: Timeout pracownika przy blokadzie.", id_kasy + 1);
                ZwolnijKase(id_kasy, stan, sem_id);
                return -1;
            }
        }
    }
    
    //Weryfikacja wieku przy alkoholu
    if (ma_alkohol) {
        ZapiszLogF(LOG_INFO, "Kasa samoobslugowa [%d]: Alkohol wykryty! Wzywam pracownika...",
                id_kasy + 1);
        
        int wynik_weryfikacji = WyslijZadanieObslugi(id_kasy, OP_WERYFIKACJA_WIEKU, wiek);
        
        if (wynik_weryfikacji == -1) {
            ZapiszLogF(LOG_OSTRZEZENIE, "Kasa samoobslugowa [%d]: Blad komunikacji z pracownikiem (ewakuacja?)", id_kasy + 1);
            return -3; // Traktuj jako blad techniczny/ewakuacje
        } else if (wynik_weryfikacji == 0) {
            ZapiszLogF(LOG_BLAD, "Kasa samoobslugowa [%d]: ODMOWA! Klient [ID: %d] niepelnoletni (wiek: %d)",
                    id_kasy + 1, id_klienta, wiek);
            return -2;
        } else {
            ZapiszLogF(LOG_INFO, "Kasa samoobslugowa [%d]: Weryfikacja wieku OK (wiek: %d)", id_kasy + 1, wiek);
        }
    }
    
    ZapiszLogF(LOG_INFO, "Kasa samoobslugowa [%d]: Klient [ID: %d] zaplacil karta. Suma: %.2f PLN. Paragon wydrukowany.",
            id_kasy + 1, id_klienta, suma);
    
    return 0;
}

//Glowna funkcja procesu kasy samoobslugowej
#ifdef KASA_SAMO_STANDALONE
int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uzycie: %s <id_kasy>\n", argv[0]);
        return 1;
    }
    
    int id_kasy = atoi(argv[1]);
    
    if (id_kasy < 0 || id_kasy >= LICZBA_KAS_SAMOOBSLUGOWYCH) {
        fprintf(stderr, "Nieprawidlowy ID kasy: %d (dozwolone: 0-%d)\n", 
                id_kasy, LICZBA_KAS_SAMOOBSLUGOWYCH - 1);
        return 1;
    }
    
    StanSklepu* stan_sklepu;
    int sem_id;
    
    if (InicjalizujProcesPochodny(&stan_sklepu, &sem_id, "Kasa samoobslugowa") == -1) {
        return 1;
    }
    
    struct sigaction sa;
    sa.sa_handler = ObslugaSygnaluWyjscia;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);  // SIGQUIT jako alias dla SIGTERM
    
    srand(time(NULL) ^ getpid());
    
    ZapiszLogF(LOG_INFO, "Kasa samoobslugowa [%d]: Proces uruchomiony.", id_kasy + 1);
    
    //Pobierz kolejke komunikatow (DEDYKOWANA DLA SAMOOBSLUGI)
    int msg_id = msgget(KLUCZ_KOLEJKI, 0600);
    if (msg_id == -1) {
        ZapiszLogF(LOG_BLAD, "Kasa samoobslugowa [%d]: Blad dolaczenia do kolejki komunikatow!", id_kasy + 1);
        OdlaczPamiecWspoldzielona(stan_sklepu);
        return 1;
    }

    //Glowna petla - Worker przetwarzajacy zgloszenia z kolejki (typ=3)
    while (1) {
        //Reaguj na SIGTERM lub flage ewakuacji
        if (CZY_KONCZYC(stan_sklepu)) {
            ZapiszLogF(LOG_INFO, "Kasa samoobslugowa [%d]: Otrzymano sygnal zakonczenia - koncze prace.", id_kasy + 1);
            break;
        }
        
        //Czekaj na klienta (typ=3)
        Komunikat msg;
        //Rozmiar danych bez mtype
        size_t msg_size = sizeof(Komunikat) - sizeof(long);
        
        if (msgrcv(msg_id, &msg, msg_size, 3, 0) != -1) {
            //Jest klient
            int id_klienta = msg.id_klienta;
            
            //Zajmij kase (dla statystyk i wizualizacji)
            ZajmijKase(id_kasy, id_klienta, stan_sklepu, sem_id);
            
            //Obsluz klienta (logika przeniesiona z klienta do kasy)
            int wynik = ObsluzKlientaSamoobslugowo(id_kasy, id_klienta, msg.liczba_produktow, 
                                                   msg.suma_koszyka, msg.ma_alkohol, msg.wiek,
                                                   stan_sklepu, sem_id);
            
            //Wyslij wynik do klienta (mtype = 100 + id_klienta)
            //W polu id_klienta przesylamy KOD WYNIKU (0=OK, inne=Blad)
            Komunikat res;
            res.mtype = 100 + id_klienta;
            res.id_klienta = wynik; 
            res.liczba_produktow = id_kasy; // Zwracamy ID kasy zebys klient wiedzial gdzie byl
            msgsnd(msg_id, &res, msg_size, 0);
            
            //Zwolnij kase
            ZwolnijKase(id_kasy, stan_sklepu, sem_id);
            
        } else {
             if (errno == EINTR) continue;
             perror("Kasa Samoobslugowa msgrcv error");
             ZapiszLogF(LOG_BLAD, "Kasa Samoobslugowa [%d]: Blad msgrcv (errno=%d)", id_kasy + 1, errno);
             break;
        }
    }
    
    OdlaczPamiecWspoldzielona(stan_sklepu);
    return 0;
}
#endif
