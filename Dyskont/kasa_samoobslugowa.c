#include "kasa_samoobslugowa.h"
#include "pracownik_obslugi.h"
#include "wspolne.h"
#include "kolejki.h"
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

//Globalne flagi dla procesu kasjera
static volatile sig_atomic_t g_alarm_timeout = 0;
//Globalne flagi dla managera
static volatile sig_atomic_t g_manager_running = 1;

//Handler dla alarmu
void ObslugaSIGALRM(int sig) {
    (void)sig;
    g_alarm_timeout = 1;
}

//Handler dla SIGTERM (Manager)
void ObslugaSIGTERM_Manager(int sig) {
    (void)sig;
    g_manager_running = 0;
}

//Zajmuje kase dla klienta
int ZajmijKase(int id_kasy, int id_klienta, StanSklepu* stan, int sem_id) {
    if (!stan || id_kasy < 0 || id_kasy >= LICZBA_KAS_SAMOOBSLUGOWYCH) return -1;
    
    if (ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA) != 0) return -1;
    
    int wynik = -1;
    if (stan->kasy_samo[id_kasy].stan == KASA_WOLNA) {
        stan->kasy_samo[id_kasy].stan = KASA_ZAJETA;
        stan->kasy_samo[id_kasy].id_klienta = id_klienta;
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
    ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
}

//Oblicza wymagana liczbe kas wedlug reguly ze 1 kasa jest dla K klientow (ale nie mniej niz 3)
unsigned int ObliczWymaganaLiczbeKas(unsigned int liczba_klientow) {
    unsigned int wymagane = (liczba_klientow + KLIENCI_NA_KASE - 1) / KLIENCI_NA_KASE;
    if (wymagane < (unsigned int)MIN_KAS_SAMO_CZYNNYCH) wymagane = (unsigned int)MIN_KAS_SAMO_CZYNNYCH;
    if (wymagane > (unsigned int)LICZBA_KAS_SAMOOBSLUGOWYCH) wymagane = (unsigned int)LICZBA_KAS_SAMOOBSLUGOWYCH;
    return wymagane;
}

//Obsluga klienta przy kasie samoobslugowej (WORKER)
int ObsluzKlientaSamoobslugowo(int id_kasy, int id_klienta, unsigned int liczba_produktow, double suma, int ma_alkohol, unsigned int wiek, StanSklepu* stan, int sem_id) {
    ZapiszLogF(LOG_INFO, "Kasa samoobslugowa [%d]: Klient [ID: %d] rozpoczyna skanowanie %u produktow", id_kasy + 1, id_klienta, liczba_produktow);
    
    //Skanowanie produktow
    for (unsigned int i = 0; i < liczba_produktow; i++) {
        SYMULACJA_USLEEP(stan, CZAS_SKANOWANIA_PRODUKTU_MS * 1000);
        
        //Losowa blokada kasy
        if (rand() % SZANSA_BLOKADY == 0) {
            ZapiszLogF(LOG_OSTRZEZENIE, "Kasa samoobslugowa [%d]: BLOKADA! Niezgodnosc wagi produktu.", id_kasy + 1);
            
            if (ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA) != 0) return -3;
            ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
            
            //Wyslanie zadania do pracownika obslugi
            if (WyslijZadanieObslugi(id_kasy, OP_ODBLOKOWANIE_KASY, 0) == 1) {

                //Pracownik odblokowal
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
        ZapiszLogF(LOG_INFO, "Kasa samoobslugowa [%d]: Alkohol wykryty! Wzywam pracownika...", id_kasy + 1);
        
        int wynik_weryfikacji = WyslijZadanieObslugi(id_kasy, OP_WERYFIKACJA_WIEKU, wiek);
        
        if (wynik_weryfikacji == -1) {
            ZapiszLogF(LOG_OSTRZEZENIE, "Kasa samoobslugowa [%d]: Blad komunikacji z pracownikiem (ewakuacja?)", id_kasy + 1);
            return -3; //Blad techniczny/ewakuacja
        } else if (wynik_weryfikacji == 0) {
            ZapiszLogF(LOG_BLAD, "Kasa samoobslugowa [%d]: ODMOWA! Klient [ID: %d] niepelnoletni (wiek: %u)", id_kasy + 1, id_klienta, wiek);
            return -2;
        } else ZapiszLogF(LOG_INFO, "Kasa samoobslugowa [%d]: Weryfikacja wieku OK (wiek: %u)", id_kasy + 1, wiek);
    }
    
    ZapiszLogF(LOG_INFO, "Kasa samoobslugowa [%d]: Klient [ID: %d] zaplacil karta. Suma: %.2f PLN. Paragon wydrukowany.",
            id_kasy + 1, id_klienta, suma);
    
    return 0;
}

//Proces potomny - Kasjer Samoobslugowy
void ProcesKasjeraSamoobslugowego(int id_kasy, StanSklepu* stan_sklepu, int sem_id, int msg_id) {
    srand(time(NULL) ^ getpid());
    
    struct sigaction sa_alarm;
    sa_alarm.sa_handler = ObslugaSIGALRM;
    sigemptyset(&sa_alarm.sa_mask);
    sa_alarm.sa_flags = 0;
    sigaction(SIGALRM, &sa_alarm, NULL);

    ZapiszLogF(LOG_INFO, "Kasa samoobslugowa [%d]: Worker uruchomiony (PID: %d).", id_kasy + 1, getpid());

    while (1) {
        MsgKasaSamo msg;
        size_t msg_size = sizeof(MsgKasaSamo) - sizeof(long);

        //Czekamy na klienta lub polecenie zamkniecia
        int odb_res = OdbierzKomunikat(msg_id, &msg, msg_size, 0, 0);

        if (odb_res != -1) {
            
            //Sprawdz czy to komenda zamkniecia
            if (msg.id_klienta == POLECENIE_ZAMKNIECIA) {
                //Weryfikacja czy mozemy zamknac kase (zapytanie do wspolnej pamieci)
                int mozna_zamknac = 0;
                
                ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
                //Manager juz podjal decyzje, ale dla pewnosci sprawdzamy regule N>3
                if (stan_sklepu->liczba_czynnych_kas_samoobslugowych > MIN_KAS_SAMO_CZYNNYCH) {
                    stan_sklepu->liczba_czynnych_kas_samoobslugowych--;
                    stan_sklepu->kasy_samo[id_kasy].stan = KASA_ZAMKNIETA;
                    mozna_zamknac = 1;
                }
                ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);

                if (mozna_zamknac) {
                    ZapiszLogF(LOG_INFO, "Kasa samoobslugowa [%d]: Otrzymano polecenie ZAMKNIECIA. Koncze prace.", id_kasy + 1);
                    break; //Exit loop -> process ends
                } else {
                     ZapiszLogF(LOG_OSTRZEZENIE, "Kasa samoobslugowa [%d]: Otrzymano polecenie ZAMKNIECIA, ale nie spelniono warunkow (N<=3). Ignoruje.", id_kasy + 1);
                     continue;
                }
            }

            //Sprawdzamy timer (czy klient nie zrezygnowal)
            if (time(NULL) - msg.timestamp > CZAS_OCZEKIWANIA_T) {
                 ZapiszLogF(LOG_OSTRZEZENIE, "Kasa samoobslugowa [%d]: Pominieto przedawniony komunikat klienta %d.", id_kasy + 1, msg.id_klienta);
                 continue;
            }

            //Zajmij kase w logice (opcjonalne, bo i tak przetwarzamy)
            ZajmijKase(id_kasy, msg.id_klienta, stan_sklepu, sem_id);

            //Obsluga
            int wynik = ObsluzKlientaSamoobslugowo(id_kasy, msg.id_klienta, msg.liczba_produktow, msg.suma_koszyka, msg.ma_alkohol, msg.wiek, stan_sklepu, sem_id);

            //Odpowiedz (VIP)
            MsgKasaSamo res;
            res.mtype = MSG_RES_SAMOOBSLUGA_BASE + msg.id_klienta;
            res.id_klienta = wynik;
            res.liczba_produktow = id_kasy; //Zwroc ID kasy
            
            WyslijKomunikatVIP(sem_id, msg_id, &res, msg_size);

            ZwolnijKase(id_kasy, stan_sklepu, sem_id);

        } else {
             if (errno == EINTR) continue;
             ZapiszLogF(LOG_BLAD, "Kasa samoobslugowa [%d]: Blad msgrcv (errno=%d)", id_kasy + 1, errno);
             break;
        }
    }
}


//Proces Managera
void ProcesManagerKas(StanSklepu* stan, int sem_id, int msg_id) {
    ZapiszLog(LOG_INFO, "Manager kas samoobslugowych uruchomiony.");
    
    //Inicjalizacja: Uruchom 3 minimalne kasy
    //Najpierw czyszczenie stanu
    ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    stan->liczba_czynnych_kas_samoobslugowych = 0;
    for(int i=0; i<LICZBA_KAS_SAMOOBSLUGOWYCH; i++) {
        stan->kasy_samo[i].stan = KASA_ZAMKNIETA;
        stan->kasy_samo[i].pid = 0;
        stan->kasy_samo[i].id_klienta = -1;
    }
    ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);

    //Petla glowna managera
    while (g_manager_running) {
        
        //1. Sprawdzanie i czyszczenie martwych procesow
        int status;
        pid_t dead_pid;
        while ((dead_pid = waitpid(-1, &status, WNOHANG)) > 0) {
            ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
            for(int i=0; i<LICZBA_KAS_SAMOOBSLUGOWYCH; i++) {
                if (stan->kasy_samo[i].pid == dead_pid) {
                    stan->kasy_samo[i].pid = 0;
                    stan->kasy_samo[i].stan = KASA_ZAMKNIETA;
                    //Nie zmniejszamy licznika 'liczba_czynnych_kas_samoobslugowych' tutaj, bo worker robi to przed wyjsciem
                    //Ale dla bezpieczenstwa:
                    //stan->liczba_czynnych_kas_samoobslugowych--; //(To moze byc ryzykowne jesli worker tez to robi)
                    //Zalozmy ze worker robi to poprawnie
                    ZapiszLogF(LOG_INFO, "Manager: Proces kasy %d [PID: %d] zakonczyl dzialanie.", i+1, dead_pid);
                }
            }
            ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
        }

        //2. Skalowanie
        ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
        unsigned int liczba_klientow = stan->liczba_klientow_w_sklepie;
        unsigned int aktywne = 0;
        for(int i=0; i<LICZBA_KAS_SAMOOBSLUGOWYCH; i++) {
            if (stan->kasy_samo[i].stan != KASA_ZAMKNIETA) aktywne++;
        }
        //Korekta licznika jesli sie rozjechal
        if (stan->liczba_czynnych_kas_samoobslugowych != aktywne) stan->liczba_czynnych_kas_samoobslugowych = aktywne;
        
        unsigned int wymagane = ObliczWymaganaLiczbeKas(liczba_klientow);
        
        //Logika otwierania
        if (aktywne < wymagane) {
             for (int i=0; i<LICZBA_KAS_SAMOOBSLUGOWYCH && aktywne < wymagane; i++) {
                 if (stan->kasy_samo[i].stan == KASA_ZAMKNIETA) {
                     //Startujemy nowa kase
                     pid_t pid = fork();
                     if (pid == 0) {
                         //Dziecko
                         stan->kasy_samo[i].stan = KASA_WOLNA; //Inicjalizacja przez dziecko lub rodzica, bezpieczniej jak rodzic ale ok
                         ProcesKasjeraSamoobslugowego(i, stan, sem_id, msg_id);
                         exit(0);
                     } else if (pid > 0) {
                         //Rodzic
                         stan->kasy_samo[i].pid = pid;
                         stan->kasy_samo[i].stan = KASA_WOLNA;
                         aktywne++;
                         stan->liczba_czynnych_kas_samoobslugowych = aktywne;
                         ZapiszLogF(LOG_INFO, "Manager: Uruchomiono kase %d [PID: %d] (Wymagane: %d)", i+1, pid, wymagane);
                     } else {
                         ZapiszLogF(LOG_BLAD, "Manager: Blad fork dla kasy %d", i+1);
                     }
                 }
             }
        }
        //Logika zamykania
        else if (aktywne > MIN_KAS_SAMO_CZYNNYCH && aktywne > wymagane) {
            unsigned int prog_zamykania = KLIENCI_NA_KASE * (aktywne - 3);
            if (liczba_klientow < prog_zamykania) {
                //Wyslij polecenie zamkniecia
                MsgKasaSamo cmd;
                cmd.mtype = MSG_TYPE_SAMOOBSLUGA; //Worker odbiera typ 3
                cmd.id_klienta = POLECENIE_ZAMKNIECIA;
                
                //Wysylamy do kolejki. Pierwszy wolny worker odbierze i sie wylaczy.
                //Nie wiemy ktory, ale to nie szkodzi, bo waitpid() to obsluzy.
                if (WyslijKomunikat(msg_id, &cmd, sizeof(MsgKasaSamo)-sizeof(long)) == 0) {
                    ZapiszLogF(LOG_INFO, "Manager: Wyslano POLECENIE_ZAMKNIECIA (Regula N-3).");
                    //Sleep zeby nie wyslac miliona polecen w jednej chwili
                    usleep(500000); 
                }
            }
        }

        ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
        
        //Krotki sleep zeby nie palic CPU
        usleep(100000); //100ms
    }

    //Ewakuacja / Koniec - zabijanie dzieci
    ZapiszLog(LOG_INFO, "Manager: Koniec pracy. Zabijam kasy samoobslugowe...");
    for(int i=0; i<LICZBA_KAS_SAMOOBSLUGOWYCH; i++) {
        ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
        pid_t pid = stan->kasy_samo[i].pid;
        ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
        
        if (pid > 0) {
            kill(pid, SIGTERM);
        }
    }
    //Czekamy na wszystkie
    while(wait(NULL) > 0);
    ZapiszLog(LOG_INFO, "Manager: Wszystkie kasy zakonczone. Exit.");
}


//Main wrapper
#ifdef KASA_SAMO_STANDALONE
int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    
    StanSklepu* stan_sklepu;
    int sem_id;
    if (InicjalizujProcesPochodny(&stan_sklepu, &sem_id, "Kasa Samo-Manager") == -1) return 1;

    int msg_id = PobierzIdKolejki(ID_IPC_SAMO);
    if (msg_id == -1) {
        ZapiszLog(LOG_BLAD, "Manager: Blad IPC (kolejka).");
        return 1;
    }

    //Obsluga sygnalow
    struct sigaction sa;
    sa.sa_handler = ObslugaSIGTERM_Manager;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    ProcesManagerKas(stan_sklepu, sem_id, msg_id);
    
    OdlaczPamiecWspoldzielona(stan_sklepu);
    return 0;
}
#endif
