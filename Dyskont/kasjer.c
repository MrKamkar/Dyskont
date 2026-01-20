#include "kasjer.h"
#include "wspolne.h"
#include "kolejki.h"
#include <string.h>
#include <signal.h>
#include <sys/msg.h>

//Zmienne globalne dla obslugi sygnalow
static volatile sig_atomic_t g_alarm_timeout = 0;

//Handler dla alarmu - przerywa msgrcv aby sprawdzic stan sklepu
void ObslugaSIGALRM(int sig) {
    (void)sig;
    g_alarm_timeout = 1;
}

#ifndef KASJER_STANDALONE
/*
#if 0
//Tworzy nowego kasjera
Kasjer* StworzKasjera(int id_kasy) {


    if (id_kasy < 0 || id_kasy >= LICZBA_KAS_STACJONARNYCH) {
        return NULL;
    }
    
    Kasjer* kasjer = (Kasjer*)malloc(sizeof(Kasjer));
    if (!kasjer) return NULL;
    
    kasjer->id_kasy = id_kasy;
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
    
    //Symulacja skanowania
    for (int i = 0; i < liczba_produktow; i++) {
        SYMULACJA_USLEEP(stan, CZAS_SKASOWANIA_PRODUKTU_MS * 1000);
    }
    
    ZapiszLogF(LOG_INFO, "Kasjer [Kasa %d]: Zakonczono obsluge klienta [ID: %d]. Suma: %.2f PLN",
            kasjer->id_kasy + 1, id_klienta, suma);
    
    kasjer->czas_ostatniej_aktywnosci = time(NULL);
}

//Przeniesienie klientow z kolejki kasy 1 do kasy 2
int PrzeniesKlientowDoKasy2(StanSklepu* stan, int sem_id, int msg_id) {
    if (!stan || msg_id < 0) return 0;
    
    //Blokada pamieci wspoldzielonej na czas odczytu liczby klientow
    if (ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA) != 0) return 0;
    
    //Sprawdz czy Kasa 2 jest otwarta
    if (stan->kasy_stacjonarne[1].stan != KASA_WOLNA && stan->kasy_stacjonarne[1].stan != KASA_ZAJETA) {
        ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
        return 0;
    }

    //Sprawdz czy Kasa 1 jest otwarta
    if (stan->kasy_stacjonarne[0].stan != KASA_WOLNA && stan->kasy_stacjonarne[0].stan != KASA_ZAJETA) {
        ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
        return 0;
    }
    
    ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    return 0;
}
#endif
    /*
    //unsigned int w_kolejce = stan->kasy_stacjonarne[0].liczba_w_kolejce;
    //unsigned int w_kolejce_2 = stan->kasy_stacjonarne[1].liczba_w_kolejce;
    ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    
    //Przenies polowe klientow z kasy 1, jesli jest miejsce w kasie 2
    unsigned int do_przeniesienia = w_kolejce / 2;
    
    //Sprawdzenie ile wolnego miejsca jest w kasie 2
    unsigned int wolne_miejsce_w_2 = (MAX_KOLEJKA_STACJONARNA > w_kolejce_2) ? (MAX_KOLEJKA_STACJONARNA - w_kolejce_2) : 0;

    //Jeśli jest więcej klientów w kolejce niż miejsca w kasie 2, zmniejsz liczbę klientów do przeniesienia
    if (do_przeniesienia > wolne_miejsce_w_2) do_przeniesienia = wolne_miejsce_w_2;
    
    int przeniesiono = 0;
    
    for (unsigned int i = 0; i < do_przeniesienia; i++) {
        MsgKasaStacj msg;
        //Odbierz klienta z kolejki 1
        if (OdbierzKomunikat(msg_id, &msg, sizeof(MsgKasaStacj) - sizeof(long), MSG_TYPE_KASA_1, 0) != -1) {
            
            //Zmien typ na Kasa 2
            msg.mtype = MSG_TYPE_KASA_2;
            
            //Wyslij klienta do kolejki 2
            if (WyslijKomunikat(msg_id, &msg, sizeof(MsgKasaStacj) - sizeof(long)) != -1) {
                przeniesiono++;
                
                //Aktualizacja licznikow po udanym przeniesieniu
                //ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA, stan);
                //if (stan->kasy_stacjonarne[0].liczba_w_kolejce > 0)
                //    stan->kasy_stacjonarne[0].liczba_w_kolejce--;
                //stan->kasy_stacjonarne[1].liczba_w_kolejce++;
                //ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
            } else {
                msg.mtype = MSG_TYPE_KASA_1;
                WyslijKomunikat(msg_id, &msg, sizeof(MsgKasaStacj) - sizeof(long));
            }
        } 
    }
    
    if (przeniesiono > 0) {
        ZapiszLogF(LOG_INFO, "Kierownik: Przeniesiono %d klientow z kasy 1 do kasy 2 (IPC).", przeniesiono);
    }
    
    return przeniesiono;
}

//Rozpoczecie dzialania kasjera
#ifdef KASJER_STANDALONE
int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uzycie: %s <id_kasy>\n", argv[0]);
        return 1;
    }
    
    int id_kasy = atoi(argv[1]);
    
    //Sprawdzenie poprawnosci ID kasy
    if (id_kasy < 0 || id_kasy >= LICZBA_KAS_STACJONARNYCH) {
        fprintf(stderr, "Nieprawidlowy ID kasy: %d (dozwolone: 0-%d)\n", 
                id_kasy, LICZBA_KAS_STACJONARNYCH - 1);
        return 1;
    }
    
    StanSklepu* stan_sklepu;
    int sem_id;
    
    //Inicjalizacja procesu pochodnego
    if (InicjalizujProcesPochodny(&stan_sklepu, &sem_id, "Kasjer") == -1) {
        return 1;
    }
    
    //Stworzenie kasjera
    Kasjer* kasjer = StworzKasjera(id_kasy);
    if (!kasjer) {
        fprintf(stderr, "Kasjer [Kasa %d]: Nie udalo sie utworzyc kasjera\n", id_kasy + 1);
        OdlaczPamiecWspoldzielona(stan_sklepu);
        return 1;
    }
    
    //Obsluga sygnalow
    struct sigaction sa;
    sa.sa_handler = ObslugaSygnaluWyjscia;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL); 

    struct sigaction sa_alarm;
    sa_alarm.sa_handler = ObslugaSIGALRM;
    sigemptyset(&sa_alarm.sa_mask);
    sa_alarm.sa_flags = 0;
    sigaction(SIGALRM, &sa_alarm, NULL);

    ZapiszLogF(LOG_INFO, "Kasjer [Kasa %d]: Proces uruchomiony, oczekuje na otwarcie kasy.", id_kasy + 1);

    
    //Dolaczenie do odpowiedniej kolejki komunikatow
    char id_projektu;
    if (id_kasy == 0) id_projektu = ID_IPC_KASA_1;
    else id_projektu = ID_IPC_KASA_2;
    
    int msg_id = PobierzIdKolejki(id_projektu);
    if (msg_id == -1) {
        ZapiszLogF(LOG_BLAD, "Kasjer [Kasa %d]: Blad dolaczenia do kolejki komunikatow!", id_kasy + 1);
        UsunKasjera(kasjer);
        OdlaczPamiecWspoldzielona(stan_sklepu);
        return 1;
    }
    

    //Glowna petla kasjera
    while (1) {
        //Reaguj na SIGTERM lub flage ewakuacji
        
            ZapiszLogF(LOG_INFO, "Kasjer [Kasa %d]: Otrzymano sygnal zakonczenia - koncze prace.", id_kasy + 1);
            break;
        
        //Sprawdzenie stanu kasy i polecen
        if (ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA) != 0) break;
        StanKasy stan_kasy = stan_sklepu->kasy_stacjonarne[id_kasy].stan;
        //unsigned int w_kolejce = stan_sklepu->kasy_stacjonarne[id_kasy].liczba_w_kolejce;
        int polecenie = stan_sklepu->polecenie_kierownika;
        int kasa_do_zamkniecia = stan_sklepu->id_kasy_do_zamkniecia;
        
        //Reakcja na polecenie zamkniecia kasy
        if (polecenie == POLECENIE_ZAMKNIJ_KASE && kasa_do_zamkniecia == id_kasy && stan_kasy != KASA_ZAMKNIETA && stan_kasy != KASA_ZAMYKANA) {
            if (w_kolejce > 0) {
                stan_sklepu->kasy_stacjonarne[id_kasy].stan = KASA_ZAMYKANA;
                stan_kasy = KASA_ZAMYKANA;
                ZapiszLogF(LOG_INFO, "Kasjer [Kasa %d]: Polecenie zamkniecia - przechodze w tryb ZAMYKANA (kolejka: %u).", id_kasy + 1, w_kolejce);
                
                //Wyczysc polecenie bo juz zareagowalismy
                stan_sklepu->polecenie_kierownika = POLECENIE_BRAK;
                stan_sklepu->id_kasy_do_zamkniecia = -1;
            }
        }

        
        //Automatyczne otwieranie kasy gdy w kolejce >= 3 osoby dla kasy 1
        if (id_kasy == 0 && stan_kasy == KASA_ZAMKNIETA && w_kolejce >= 3) {
            stan_sklepu->kasy_stacjonarne[id_kasy].stan = KASA_WOLNA;
            stan_kasy = KASA_WOLNA;
            ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
            
            ZapiszLogF(LOG_INFO, "Kasjer [Kasa %d]: Automatyczne otwarcie kasy (>= 3 osoby w kolejce).", id_kasy + 1);
        } else {
            ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
        }
        
        if (stan_kasy == KASA_ZAMKNIETA) {
            
            //Czekanie na sygnal otwarcia
            ZajmijSemafor(sem_id, SEM_OTWORZ_KASA_STACJONARNA(id_kasy));
            continue;
        }
        
        //Odbierz klienta z kolejki komunikatow
        MsgKasaStacj msg_in;
        size_t msg_size = sizeof(MsgKasaStacj) - sizeof(long);
        
        //Ustawiamy alarm, aby nie blokowac sie w nieskonczonosc i moc np. zareagowac na ewakuacje
        alarm(CZAS_OCZEKIWANIA_T);
        
        //Odbior blokujacy - moze zostac przerwany sygnalem SIGALRM
        int res = OdbierzKomunikat(msg_id, &msg_in, msg_size, 0, 0);
        
        //Wylaczamy alarm
        alarm(0);
        
        if (res != -1) {

            //Sprawdzamy czy komunikat nie jest za stary (klient mogl juz zrezygnowac)
            if (time(NULL) - msg_in.timestamp > CZAS_OCZEKIWANIA_T) {
                ZapiszLogF(LOG_OSTRZEZENIE, "Kasjer %d: Pominieto przedawniony komunikat klienta %d.", id_kasy + 1, msg_in.id_klienta);
                continue;
            }

            int id_klienta = msg_in.id_klienta;

            int liczba_produktow = msg_in.liczba_produktow;
            double suma = msg_in.suma_koszyka;
            
            //Zaktualizuj stan kasy
            if (ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA) == 0) {
                if (stan_sklepu->kasy_stacjonarne[id_kasy].stan == KASA_WOLNA) {
                    stan_sklepu->kasy_stacjonarne[id_kasy].stan = KASA_ZAJETA;
                }
                stan_sklepu->kasy_stacjonarne[id_kasy].id_klienta = id_klienta;
                ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
            }
            
            //Symulacja obslugi klienta
            ObsluzKlienta(kasjer, id_klienta, liczba_produktow, suma, stan_sklepu);
            
            //Wyslij potwierdzenie do klienta (VIP, by nie blokowalo sie na zarezerwowanym miejscu)
            MsgKasaStacj msg_out;
            msg_out.mtype = MSG_RES_STACJONARNA_BASE + id_klienta;
            msg_out.id_klienta = id_klienta;
            msg_out.liczba_produktow = id_kasy; // Zwracamy ID kasy
            WyslijKomunikatVIP(sem_id, msg_id, &msg_out, msg_size);



            
            //Zwolnij kase i zaktualizuj licznik kolejki
            if (ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA) == 0) {
                //if (stan_sklepu->kasy_stacjonarne[id_kasy].liczba_w_kolejce > 0) {
                //     stan_sklepu->kasy_stacjonarne[id_kasy].liczba_w_kolejce--;
                //}
                
                //Sprawdz polecenie zamkniecia po obsluzeniu klienta
                int polecenie = stan_sklepu->polecenie_kierownika;
                int kasa_do_zamkniecia = stan_sklepu->id_kasy_do_zamkniecia;
                StanKasy obecny_stan = stan_sklepu->kasy_stacjonarne[id_kasy].stan;
                
                //Reakcja na polecenie zamkniecia
                if (polecenie == POLECENIE_ZAMKNIJ_KASE && kasa_do_zamkniecia == id_kasy && obecny_stan != KASA_ZAMKNIETA && obecny_stan != KASA_ZAMYKANA) {

                   // if (stan_sklepu->kasy_stacjonarne[id_kasy].liczba_w_kolejce > 0) {
                        stan_sklepu->kasy_stacjonarne[id_kasy].stan = KASA_ZAMYKANA;
                        obecny_stan = KASA_ZAMYKANA;
                        int kolejka = PobierzRozmiarKolejki(msg_id);
                        ZapiszLogF(LOG_INFO, "Kasjer [Kasa %d]: Polecenie zamkniecia - przechodze w tryb ZAMYKANA (kolejka: %u).", 
                                id_kasy + 1, kolejka);
                        
                        //Wyczysc polecenie bo juz zareagowalismy na nie
                        stan_sklepu->polecenie_kierownika = POLECENIE_BRAK;
                        stan_sklepu->id_kasy_do_zamkniecia = -1;
                    }
                }
                
                //Logika zakonczenia obslugi
                //ejce == 0) {
                     
                    //Jezeli jest w trybie ZAMYKANA i kolejka jest pusta to ustawiamy stan na ZAMKNIETA
                     //stan_sklepu->kasy_stacjonarne[id_kasy].stan = KASA_ZAMKNIETA;
                    // ZapiszLogF(LOG_INFO, "Kasjer [Kasa %d]: Kolejka pusta - zamykam kase.", id_kasy + 1);

                } else if (obecny_stan == KASA_ZAJETA) {

                     //Jesli byla ZAJETA to ustawiamy stan na WOLNA
                     stan_sklepu->kasy_stacjonarne[id_kasy].stan = KASA_WOLNA;
                }

                //Jesli byla ZAMYKANA i kolejka > 0 to dalej jest ZAMYKANA
                stan_sklepu->kasy_stacjonarne[id_kasy].id_klienta = -1;
                ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
            }
        } 
        else {
            if (errno == EINTR) {
                //Jesli przerwano sygnalem to sprawdzamy CZY_KONCZYC
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
*/
#endif

#ifdef KASJER_STANDALONE
int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    return 0;
}
#endif