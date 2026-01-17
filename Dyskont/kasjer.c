#include "kasjer.h"
#include "wspolne.h"
#include <string.h>
#include <signal.h>
#include <sys/msg.h>

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



//Obsluga klienta przez kasjera
void ObsluzKlienta(Kasjer* kasjer, int id_klienta, int liczba_produktow, double suma, StanSklepu* stan) {
    if (!kasjer) return;
    
    ZapiszLogF(LOG_INFO, "Kasjer [Kasa %d]: Rozpoczynam obsluge klienta [ID: %d], produktow: %d",
            kasjer->id_kasy + 1, id_klienta, liczba_produktow);
    
    kasjer->stan = KASJER_OBSLUGUJE;
    
    //Symulacja skanowania (pomijana w trybie testu 1, sprawdzane wewnatrz makra - ale makro sprawdza stan->tryb_testu)
    for (int i = 0; i < liczba_produktow; i++) {
        SYMULACJA_USLEEP(stan, CZAS_OBSLUGI_PRODUKTU_MS * 1000);
    }
    
    ZapiszLogF(LOG_INFO, "Kasjer [Kasa %d]: Zakonczono obsluge klienta [ID: %d]. Suma: %.2f PLN",
            kasjer->id_kasy + 1, id_klienta, suma);
    
    kasjer->czas_ostatniej_aktywnosci = time(NULL);
    kasjer->stan = KASJER_CZEKA_NA_KLIENTA;
}

//Migracja klientow z kolejki kasy 1 do kasy 2 (IPC - przepiecie komunikatow)
int MigrujKlientowDoKasy2(StanSklepu* stan, int sem_id, int msg_id) {
    if (!stan || msg_id < 0) return 0;
    
    //Blokada obu kas
    if (ZajmijSemafor(sem_id, MUTEX_KASY(0)) != 0) return 0;
    if (ZajmijSemafor(sem_id, MUTEX_KASY(1)) != 0) {
        ZwolnijSemafor(sem_id, MUTEX_KASY(0));
        return 0;
    }
    
    KasaStacjonarna* kasa1 = &stan->kasy_stacjonarne[0];
    KasaStacjonarna* kasa2 = &stan->kasy_stacjonarne[1];
    
    int przeniesiono = 0;
    
    //Przenies polowe klientow z kasy 1 (max 5 osob)
    int do_przeniesienia = kasa1->liczba_w_kolejce / 2;
    if (do_przeniesienia > 5) do_przeniesienia = 5;
    
    for (int i = 0; i < do_przeniesienia; i++) {
        Komunikat msg;
        //Odbierz z kolejki 1 bez blokowania (najstarszy)
        if (msgrcv(msg_id, &msg, sizeof(Komunikat) - sizeof(long), MSG_TYPE_KASA_1, IPC_NOWAIT) != -1) {
            
            //Zmien typ na Kasa 2
            msg.mtype = MSG_TYPE_KASA_2;
            
            //Wyslij do kolejki 2
            if (msgsnd(msg_id, &msg, sizeof(Komunikat) - sizeof(long), 0) != -1) {
                przeniesiono++;
                kasa1->liczba_w_kolejce--;
                kasa2->liczba_w_kolejce++;
            } else {
                //Fail - oddaj do kasy 1
                msg.mtype = MSG_TYPE_KASA_1;
                msgsnd(msg_id, &msg, sizeof(Komunikat) - sizeof(long), 0);
                break;
            }
        } else {
            break;
        }
    }
    
    ZwolnijSemafor(sem_id, MUTEX_KASY(1));
    ZwolnijSemafor(sem_id, MUTEX_KASY(0));
    
    if (przeniesiono > 0) {
        ZapiszLogF(LOG_INFO, "Kierownik: Przeniesiono %d klientow z kasy 1 do kasy 2 (IPC).", przeniesiono);
    }
    
    return przeniesiono;
}

//Punkt wejscia dla procesu kasjera
#ifdef KASJER_STANDALONE
int main(int argc, char* argv[]) {
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
    
    StanSklepu* stan_sklepu;
    int sem_id;
    
    if (InicjalizujProcesPochodny(&stan_sklepu, &sem_id, "Kasjer") == -1) {
        return 1;
    }
    
    Kasjer* kasjer = StworzKasjera(id_kasy);
    if (!kasjer) {
        fprintf(stderr, "Kasjer [Kasa %d]: Nie udalo sie utworzyc kasjera\n", id_kasy + 1);
        OdlaczPamiecWspoldzielona(stan_sklepu);
        return 1;
    }
    
    struct sigaction sa;
    sa.sa_handler = ObslugaSygnaluWyjscia;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);  // SIGQUIT jako alias dla SIGTERM
    
    ZapiszLogF(LOG_INFO, "Kasjer [Kasa %d]: Proces uruchomiony, oczekuje na otwarcie kasy.", id_kasy + 1);
    
    //Pobierz ID kolejki (tylko dla kas stacjonarnych - musi istniec)
    int msg_id = msgget(KLUCZ_KOLEJKI, 0600);
    if (msg_id == -1) {
        ZapiszLogF(LOG_BLAD, "Kasjer [Kasa %d]: Blad dolaczenia do kolejki komunikatow!", id_kasy + 1);
        UsunKasjera(kasjer);
        OdlaczPamiecWspoldzielona(stan_sklepu);
        return 1;
    }
    
    //Glowna petla kasjera
    while (1) {
        //Reaguj na SIGTERM lub flage ewakuacji
        if (CZY_KONCZYC(stan_sklepu)) {
            ZapiszLogF(LOG_INFO, "Kasjer [Kasa %d]: Otrzymano sygnal zakonczenia - koncze prace.", id_kasy + 1);
            break;
        }
        
        //Sprawdzenie stanu kasy i polecen
        if (ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA) != 0) break;
        StanKasy stan_kasy = stan_sklepu->kasy_stacjonarne[id_kasy].stan;
        int w_kolejce = stan_sklepu->kasy_stacjonarne[id_kasy].liczba_w_kolejce;
        int polecenie = stan_sklepu->polecenie_kierownika;
        int kasa_do_zamkniecia = stan_sklepu->id_kasy_do_zamkniecia;
        
        //Reakcja na polecenie zamkniecia - ustaw ZAMYKANA jesli sa klienci
        if (polecenie == 2 && kasa_do_zamkniecia == id_kasy && stan_kasy != KASA_ZAMKNIETA && stan_kasy != KASA_ZAMYKANA) {
            if (w_kolejce > 0) {
                stan_sklepu->kasy_stacjonarne[id_kasy].stan = KASA_ZAMYKANA;
                stan_kasy = KASA_ZAMYKANA;
                ZapiszLogF(LOG_INFO, "Kasjer [Kasa %d]: Polecenie zamkniecia - przechodze w tryb ZAMYKANA (kolejka: %d).", id_kasy + 1, w_kolejce);
                
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
            
            ZapiszLogF(LOG_INFO, "Kasjer [Kasa %d]: Automatyczne otwarcie kasy (>= 3 osoby w kolejce).", id_kasy + 1);
        } else {
            ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
        }
        
        if (stan_kasy == KASA_ZAMKNIETA) {
            kasjer->stan = KASJER_NIEAKTYWNY;
            
            CzekajNaSemafor(sem_id, SEM_OTWORZ_KASA_STACJ(id_kasy), 2);
            continue;
        }
        
        kasjer->stan = KASJER_CZEKA_NA_KLIENTA;
        
        //Odbierz klienta z kolejki komunikatow (BLOKUJACE - Kasjer spi az przyjdzie klient)
        Komunikat msg_in;
        size_t msg_size = sizeof(Komunikat) - sizeof(long);
        
        int res = msgrcv(msg_id, &msg_in, msg_size, id_kasy + 1, 0);
        
        if (res != -1) {
            //Jest klient - obsluga
            int id_klienta = msg_in.id_klienta;
            
            //Zaktualizuj czas ostatniej obslugi
            if (ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA) == 0) {
                stan_sklepu->kasy_stacjonarne[id_kasy].stan = KASA_ZAJETA;
                stan_sklepu->kasy_stacjonarne[id_kasy].id_klienta = id_klienta;
                ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
            }
            
            //Symulacja obslugi
            int liczba_produktow = 3 + (rand() % 8);
            double suma = liczba_produktow * 10.0;
            ObsluzKlienta(kasjer, id_klienta, liczba_produktow, suma, stan_sklepu);
            
            //Wyslij potwierdzenie do klienta
            Komunikat msg_out;
            msg_out.mtype = 100 + id_klienta;
            msg_out.id_klienta = id_klienta;
            msg_out.liczba_produktow = id_kasy; // Zwracamy ID kasy
            msgsnd(msg_id, &msg_out, msg_size, 0);
            
            //Zwolnij kase
            //Zwolnij kase i zaktualizuj licznik kolejki
            if (ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA) == 0) {
                if (stan_sklepu->kasy_stacjonarne[id_kasy].liczba_w_kolejce > 0) {
                     stan_sklepu->kasy_stacjonarne[id_kasy].liczba_w_kolejce--;
                }

                if (stan_sklepu->kasy_stacjonarne[id_kasy].stan != KASA_ZAMYKANA) {
                    stan_sklepu->kasy_stacjonarne[id_kasy].stan = KASA_WOLNA;
                }
                stan_sklepu->kasy_stacjonarne[id_kasy].id_klienta = -1;
                stan_sklepu->kasy_stacjonarne[id_kasy].czas_ostatniej_obslugi = time(NULL);
                ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
            }
        } 
        else {
            //Blad odbioru (np. przerwanie sygnalem)
            if (errno == EINTR) {
                //Jesli przerwano sygnalem (np. SIGQUIT/SIGTERM), petla while(1) sprawdzi CZY_KONCZYC
                //i wyjdzie przy nastepnym obrocie.
                continue;
            }
            
            //Inny blad
            perror("msgrcv");
            break;
        }
    }

    
    //Czyszczenie
    UsunKasjera(kasjer);
    OdlaczPamiecWspoldzielona(stan_sklepu);
    
    return 0;
}
#endif
