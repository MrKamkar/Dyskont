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
unsigned int ObliczWymaganaLiczbeKas(unsigned int liczba_klientow) {
    unsigned int wymagane = (liczba_klientow + KLIENCI_NA_KASE - 1) / KLIENCI_NA_KASE;
    if (wymagane < (unsigned int)MIN_KAS_SAMO_CZYNNYCH) wymagane = (unsigned int)MIN_KAS_SAMO_CZYNNYCH;
    if (wymagane > (unsigned int)LICZBA_KAS_SAMOOBSLUGOWYCH) wymagane = (unsigned int)LICZBA_KAS_SAMOOBSLUGOWYCH;
    return wymagane;
}

#include "semafory.h"

//Aktualizuje liczbe otwartych kas samoobslugowych w zaleznosci od liczby klientow
void ZaktualizujKasySamoobslugowe(StanSklepu* stan, int sem_id, unsigned int liczba_klientow) {
    if (!stan) return;

    if (ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA) != 0) return;

    unsigned int aktywne = stan->liczba_czynnych_kas_samo;
    
    //Wymagane wg reguly "1 kasa na K klientow" (min 3)
    unsigned int wymagane = ObliczWymaganaLiczbeKas(liczba_klientow);
    
    //Jesli mamy za malo - otwieramy
    if (aktywne < wymagane) {
        for (int i=0; i<LICZBA_KAS_SAMOOBSLUGOWYCH && aktywne < wymagane; i++) {
            if (stan->kasy_samo[i].stan == KASA_ZAMKNIETA) {
                stan->kasy_samo[i].stan = KASA_WOLNA;
                aktywne++;
                
                //Budzimy kasjera
                ZwolnijSemafor(sem_id, SEM_KASA_SAMO_0 + i);
                
                ZapiszLogF(LOG_INFO, "System: Otwarto kase samoobslugowa %d (Klientow: %d, Wymagane: %d)", i+1, liczba_klientow, wymagane);
            }
        }
    }
    //Jesli mamy wiecej niz minimum (tzn > 3) i wiecej niz wymagane...
    else if (aktywne > (unsigned int)MIN_KAS_SAMO_CZYNNYCH && aktywne > wymagane) {
        //Sprawdzamy specificzna regule zamykania: L_klientow < K * (N - 3)
        //N = aktywne (obecna liczba czynnych kas)
        unsigned int prog_zamykania = KLIENCI_NA_KASE * (aktywne - 3);
        
        if (liczba_klientow < prog_zamykania) {
            //Wysylamy polecenie zamkniecia do kolejki (odbierze pierwsza wolna kasa)
            //Nie zmieniamy stanu tutaj - kasa zmieni go sama jak odbierze komunikat
            //Ale musimy uwazac zeby nie wyslac za duzo komend naraz
            
            //Dla uproszczenia (zeby nie spamowac): wysylamy tylko jesli kolejka samoobslugowa jest pusta
            //lub jesli minelo troche czasu od ostatniej zmiany (tu: brak timera w pamieci, wiec simplified)
            
             //Wyslij CMD_CLOSE (typ 3 - wspolny dla klientow i komend)
             int msg_id = msgget(KLUCZ_KOLEJKI, 0600);
             if (msg_id != -1) {
                 Komunikat cmd;
                 cmd.mtype = 3;
                 cmd.id_klienta = CMD_CLOSE; 
                 //Blokujace wysylanie zeby miec pewnosc dostarczenia
                 if (msgsnd(msg_id, &cmd, sizeof(Komunikat)-sizeof(long), 0) == 0) {
                     ZapiszLogF(LOG_INFO, "System: Wyslano polecenie zamkniecia kasy (Regula K*(N-3): Klientow=%d < %d).", liczba_klientow, prog_zamykania);
                 }
             }
        }
    }
    
    //Aktualizuj liczbę czynnych kas tylko jeśli ją zmieniliśmy (otwieranie)
    //Zamykanie jest obsługiwane przez samą kasę, która aktualizuje liczba_czynnych_kas_samo
    if (aktywne != stan->liczba_czynnych_kas_samo) {
        stan->liczba_czynnych_kas_samo = aktywne;
    }
    ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
}

//Obsluga klienta przy kasie samoobslugowej
int ObsluzKlientaSamoobslugowo(int id_kasy, int id_klienta, unsigned int liczba_produktow, 
                                 double suma, int ma_alkohol, unsigned int wiek, StanSklepu* stan, int sem_id) {
    ZapiszLogF(LOG_INFO, "Kasa samoobslugowa [%d]: Klient [ID: %d] rozpoczyna skanowanie %u produktow",
            id_kasy + 1, id_klienta, liczba_produktow);
    
    //Skanowanie produktow
    for (unsigned int i = 0; i < liczba_produktow; i++) {
        //Symulacja skanowania (pomijana w trybie testu 1)
        SYMULACJA_USLEEP(stan, CZAS_SKANOWANIA_PRODUKTU_MS * 1000);
        
        //Losowa blokada kasy
        if (rand() % SZANSA_BLOKADY == 0) {
            ZapiszLogF(LOG_OSTRZEZENIE, "Kasa samoobslugowa [%d]: BLOKADA! Niezgodnosc wagi produktu.",
                    id_kasy + 1);
            
            if (ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA) != 0) return -3;
            ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
            
            //Wyslanie zadania do pracownika obslugi (MSGQ)
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
            ZapiszLogF(LOG_BLAD, "Kasa samoobslugowa [%d]: ODMOWA! Klient [ID: %d] niepelnoletni (wiek: %u)",
                    id_kasy + 1, id_klienta, wiek);
            return -2;
        } else {
            ZapiszLogF(LOG_INFO, "Kasa samoobslugowa [%d]: Weryfikacja wieku OK (wiek: %u)", id_kasy + 1, wiek);
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
    
    StanSklepu* stan_sklepu;
    int sem_id;
    if (InicjalizujProcesPochodny(&stan_sklepu, &sem_id, "Kasa samoobslugowa") == -1) return 1;
    
    //Obsluga sygnalow...
    struct sigaction sa;
    sa.sa_handler = ObslugaSygnaluWyjscia;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL); 
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
        if (CZY_KONCZYC(stan_sklepu)) break;
        
        if (stan_sklepu->kasy_samo[id_kasy].stan == KASA_ZAMKNIETA) {
            ZapiszLogF(LOG_INFO, "Kasa samoobslugowa [%d]: ZAMKNIETA. Czekam na otwarcie...", id_kasy + 1);
            ZajmijSemafor(sem_id, SEM_KASA_SAMO_0 + id_kasy); //BLOKADA - czekamy na managera
            ZapiszLogF(LOG_INFO, "Kasa samoobslugowa [%d]: OTWARTA. Wracam do pracy.", id_kasy + 1);
            continue; //Po obudzeniu sprawdzamy wszystko od nowa
        }

        //Czekaj na klienta (typ=3)
        Komunikat msg;
        //Rozmiar danych bez mtype
        size_t msg_size = sizeof(Komunikat) - sizeof(long);
        
        if (msgrcv(msg_id, &msg, msg_size, 3, 0) != -1) {
            
            //Sprawdz czy to komenda zamkniecia
            if (msg.id_klienta == CMD_CLOSE) {
                //Weryfikacja czy MOZEMY sie zamknac (zeby nie zamknac 3-ciej kasy wyscigiem)
                int mozna_zamknac = 0;
                
                ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
                if (stan_sklepu->liczba_czynnych_kas_samo > MIN_KAS_SAMO_CZYNNYCH) {
                    stan_sklepu->liczba_czynnych_kas_samo--;
                    stan_sklepu->kasy_samo[id_kasy].stan = KASA_ZAMKNIETA;
                    mozna_zamknac = 1;
                }
                ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
                
                if (mozna_zamknac) {
                    ZapiszLogF(LOG_INFO, "Kasa samoobslugowa [%d]: Pobrano polecenie ZAMKNIECIA. Koncze prace.", id_kasy + 1);
                    continue; //W nastepnej iteracji loop zablokuje sie na semaforze
                } else {
                    //Nie mozna zamknac (np. ktos inny zamknal sie w miedzyczasie i zostalo 3)
                    ZapiszLogF(LOG_OSTRZEZENIE, "Kasa samoobslugowa [%d]: Ignoruje CMD_CLOSE (za malo aktywnych kas).", id_kasy + 1);
                    continue;
                }
            }
            
            //Obsluga klienta - bez zmian
            int id_klienta = msg.id_klienta;

            //Jesli wiadomosc jest starsza niz T, to klient juz poszedl
            if (time(NULL) - msg.timestamp > CZAS_OCZEKIWANIA_T) {
                 ZapiszLogF(LOG_INFO, "Kasa samoobslugowa [%d]: Pomijam nieaktualne zgloszenie klienta [ID: %d].", id_kasy + 1, id_klienta);
                 continue; //Nie przetwarzaj, nie wysylaj odpowiedzi, nie zmniejszaj licznika (bo klient sam zmniejszyl)
            }
            
            ZajmijKase(id_kasy, id_klienta, stan_sklepu, sem_id);
            
            //Zmniejsz licznik kolejki samoobslugowej
            if (ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA) == 0) {
                if (stan_sklepu->liczba_w_kolejce_samoobslugowej > 0) stan_sklepu->liczba_w_kolejce_samoobslugowej--;
                ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
            }
            
            int wynik = ObsluzKlientaSamoobslugowo(id_kasy, id_klienta, msg.liczba_produktow, msg.suma_koszyka, msg.ma_alkohol, msg.wiek, stan_sklepu, sem_id);
            
            Komunikat res;
            res.mtype = MSG_RES_SAMOOBSLUGA_BASE + id_klienta;
            res.id_klienta = wynik; 
            res.liczba_produktow = id_kasy; 
            msgsnd(msg_id, &res, msg_size, 0);
            
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
