#include "kasa_samoobslugowa.h"
#include "pracownik_obslugi.h"
#include "pamiec_wspoldzielona.h"
#include "kolejki.h"
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <pthread.h>

//Zajmuje kase dla klienta
int ZajmijKase(int id_kasy, int id_klienta, StanSklepu* stan, int sem_id) {
    if (!stan || id_kasy < 0 || id_kasy >= LICZBA_KAS_SAMOOBSLUGOWYCH) return -1;
    
    if (ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA) != 0) return -1;
    
    int wynik = -1;
    if (stan->kasy_samoobslugowe[id_kasy].stan == KASA_WOLNA) {
        stan->kasy_samoobslugowe[id_kasy].stan = KASA_ZAJETA;
        stan->kasy_samoobslugowe[id_kasy].id_klienta = id_klienta;
        wynik = 0;
    }
    
    ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    return wynik;
}

//Zwalnia kase
void ZwolnijKase(int id_kasy, StanSklepu* stan, int sem_id) {
    if (!stan || id_kasy < 0 || id_kasy >= LICZBA_KAS_SAMOOBSLUGOWYCH) return;
    
    if (ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA) != 0) return;
    
    stan->kasy_samoobslugowe[id_kasy].stan = KASA_WOLNA;
    stan->kasy_samoobslugowe[id_kasy].id_klienta = -1;
    
    ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
}

//Oblicza wymagana liczbe kas: 1 na K klientow, minimum 3
unsigned int ObliczWymaganaLiczbeKas(unsigned int liczba_klientow) {
    unsigned int wymagane = (liczba_klientow + KLIENCI_NA_KASE - 1) / KLIENCI_NA_KASE;
    if (wymagane < (unsigned int)MIN_KAS_SAMO_CZYNNYCH) wymagane = (unsigned int)MIN_KAS_SAMO_CZYNNYCH;
    if (wymagane > (unsigned int)LICZBA_KAS_SAMOOBSLUGOWYCH) wymagane = (unsigned int)LICZBA_KAS_SAMOOBSLUGOWYCH;
    return wymagane;
}

//Obsluga klienta przy kasie samoobslugowej
int ObsluzKlientaSamoobslugowo(int id_kasy, int id_klienta, unsigned int liczba_produktow, double suma, int ma_alkohol, unsigned int wiek, StanSklepu* stan, int sem_id) {
    ZapiszLogF(LOG_INFO, "Kasa samoobslugowa [%d]: Klient [ID: %d] skanuje %u produktow", id_kasy + 1, id_klienta, liczba_produktow);
    
    //Skanowanie produktow
    for (unsigned int i = 0; i < liczba_produktow; i++) {
        SYMULACJA_USLEEP(stan, CZAS_SKANOWANIA_PRODUKTU_MS * 1000);
        
        //Losowa blokada kasy
        if (rand() % SZANSA_BLOKADY == 0) {
            ZapiszLogF(LOG_OSTRZEZENIE, "Kasa samoobslugowa [%d]: BLOKADA!", id_kasy + 1);


            int wynik_blokady = WyslijZadanieObslugi(id_kasy, OP_ODBLOKOWANIE_KASY, 0, sem_id);
            
            if (wynik_blokady == 1) {
                if (ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA) == 0) {
                     stan->kasy_samoobslugowe[id_kasy].stan = KASA_ZAJETA;
                     ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
                }
                ZapiszLogF(LOG_INFO, "Kasa samoobslugowa [%d]: Odblokowana", id_kasy + 1);
            } else {
                 ZapiszLogF(LOG_BLAD, "Kasa samoobslugowa [%d]: Brak odpowiedzi!", id_kasy + 1);
                 ZwolnijKase(id_kasy, stan, sem_id);
                 return -1;
            }
        }
    }
    
    //Weryfikacja wieku przy alkoholu
    if (ma_alkohol) {
        ZapiszLogF(LOG_INFO, "Kasa samoobslugowa [%d]: Weryfikacja wieku..", id_kasy + 1);
        
        int wynik = WyslijZadanieObslugi(id_kasy, OP_WERYFIKACJA_WIEKU, wiek, sem_id);
        
        if (wynik == -1) {
            return -3;
        } else if (wynik == 0) {
            ZapiszLogF(LOG_BLAD, "Kasa samoobslugowa [%d]: Klient niepelnoletni!", id_kasy + 1);
            return -2;
        }
    }
    
    ZapiszLogF(LOG_INFO, "Kasa samoobslugowa [%d]: Klient [%d] zaplacil %.2f PLN", id_kasy + 1, id_klienta, suma);
    return 0;
}

#ifdef KASA_SAMO_STANDALONE

//Zmienne globalne dla obslugi sygnalow
static StanSklepu* g_stan_sklepu = NULL;
static int g_sem_id = -1;
static int g_msg_id = -1;
static int g_czy_rodzic = 1; //Domyslnie jestesmy rodzicem
static pthread_t g_watek_skalujacy;

//Flagi dla bezpiecznego zamykania
static volatile sig_atomic_t g_kasa_zajeta = 0;
static volatile sig_atomic_t g_zamkniecie_oczekujace = 0;

//Obsluga SIGUSR1 - Lagodne zamykanie przy skalowaniu
static void ObslugaSIGUSR1(int sig) {
    (void)sig;
    
    //Jesli kasa pracuje, ustawiamy flage oczekujaca
    if (g_kasa_zajeta) {
        g_zamkniecie_oczekujace = 1;
        ZapiszLogF(LOG_DEBUG, "Kasa [PID: %d] otrzymala rozkaz zamkniecia - dokanczam obsluge klienta", getpid());
    } else {
        //Jesli wolna, konczymy od razu
        if (g_stan_sklepu) OdlaczPamiecWspoldzielona(g_stan_sklepu);
        ZapiszLogF(LOG_INFO, "Kasa samoobslugowa [PID: %d]: Konczy dzialanie (SIGUSR1 - bezczynna)", getpid());
        exit(0);
    }
}

//Obsługa jednego klienta 
static int ObsluzKlienta(int id_kasy) {
    MsgKasaSamo msg;
    size_t msg_size = sizeof(MsgKasaSamo) - sizeof(long);

    //Debug: Oczekiwanie
    ZapiszLogF(LOG_DEBUG, "Kasa samoobslugowa [%d]: Czekam na klienta..", id_kasy + 1);

    //Odbieranie komunikatu bez zwalniania semafora
    int res = OdbierzKomunikat(g_msg_id, &msg, msg_size, MSG_TYPE_SAMOOBSLUGA, 0, -1, -1);

    //Jesli pomyslnie odebrano wiadomosc, zwolnij miejsce w kolejce jesli ktos czeka
    if (res != -1) {
        g_kasa_zajeta = 1;

        //Natychmiastowe zajecie kasy (blokuje przeniesienie klienta)
        ZajmijKase(id_kasy, msg.id_klienta, g_stan_sklepu, g_sem_id);

        //Sprawdzenie czy klient nie zrezygnowal tuz przed zajeciem
        if (CzyPominiety(g_stan_sklepu, g_sem_id, msg.id_klienta)) {
            ZapiszLogF(LOG_INFO, "Kasa samoobslugowa [%d]: Pomijam klienta [ID: %d] - wycofal sie", id_kasy + 1, msg.id_klienta);
            ZwolnijKase(id_kasy, g_stan_sklepu, g_sem_id);
            g_kasa_zajeta = 0;
            return 1;
        }

        int wynik = ObsluzKlientaSamoobslugowo(id_kasy, msg.id_klienta, msg.liczba_produktow, msg.suma_koszyka, msg.ma_alkohol, msg.wiek, g_stan_sklepu, g_sem_id);

        MsgKasaSamo res_msg;
        res_msg.mtype = MSG_RES_SAMOOBSLUGA_BASE + msg.id_klienta;
        res_msg.id_klienta = wynik;
        res_msg.liczba_produktow = id_kasy;
        
        //Wyslanie odpowiedzi jesli klient nadal czeka
        if (!CzyPominiety(g_stan_sklepu, g_sem_id, msg.id_klienta)) {
            WyslijKomunikatVIP(g_msg_id, &res_msg, msg_size);
        }

        ZwolnijKase(id_kasy, g_stan_sklepu, g_sem_id);
        g_kasa_zajeta = 0; 
        
        //Obsluga SIGUSR1 (skalowanie)
        if (g_zamkniecie_oczekujace) {
            ZapiszLogF(LOG_INFO, "Kasa samoobslugowa [%d]: Zamykanie po dokonczeniu obslugi", id_kasy + 1);
            if (g_stan_sklepu) OdlaczPamiecWspoldzielona(g_stan_sklepu);
            exit(0);
        }

        return 1;

    } else {
        if (errno != EINTR) ZapiszLogF(LOG_BLAD, "Kasa %d: Blad msgrcv: %d", id_kasy+1, errno);
        return (errno == EINTR) ? -1 : -2;
    }
}

//Logika procesu potomnego (kasy 1-5)
static void LogikaKasyPotomnej(int id_kasy) {
    srand(time(NULL) ^ getpid());
    ZapiszLogF(LOG_INFO, "Kasa samoobslugowa [%d]: Uruchomiona (PID: %d)", id_kasy + 1, getpid());

    int wynik;
    while ((wynik = ObsluzKlienta(id_kasy)) > 0);
    
    ZapiszLogF(LOG_INFO, "Kasa samoobslugowa [%d]: Zakonczona", id_kasy + 1);
}

//Uruchomienie nowej kasy (fork)
static pid_t UruchomKase(int slot) {
    if (slot < 0 || slot >= LICZBA_KAS_SAMOOBSLUGOWYCH) return -1;
    
    pid_t pid = fork();
    
    if (pid == 0) {
        //Dziecko ma miec odblokowane sygnaly
        sigset_t set;
        sigemptyset(&set);
        pthread_sigmask(SIG_SETMASK, &set, NULL);
        
        //Rejestracja SIGUSR1 do lagodnego zamykania kasy
        struct sigaction sa;
        sa.sa_handler = ObslugaSIGUSR1;
        sigemptyset(&sa.sa_mask); 
        sa.sa_flags = 0; 
        sigaction(SIGUSR1, &sa, NULL);
        
        g_czy_rodzic = 0;
        LogikaKasyPotomnej(slot);
        OdlaczPamiecWspoldzielona(g_stan_sklepu);
        exit(0);
        
    } else if (pid > 0) {
        ZajmijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
        g_stan_sklepu->kasy_samoobslugowe[slot].pid = pid;
        g_stan_sklepu->kasy_samoobslugowe[slot].stan = KASA_WOLNA;
        g_stan_sklepu->liczba_czynnych_kas_samoobslugowych++;
        ZwolnijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
        
        ZapiszLogF(LOG_INFO, "Uruchomiono kase %d [PID: %d]", slot + 1, pid);
    }
    
    return pid;
}

//Zamkniecie kasy (Skalowanie - SIGUSR1)
static void ZamknijKase(int slot) {
    if (slot < 0 || slot >= LICZBA_KAS_SAMOOBSLUGOWYCH) return;
    
    ZajmijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    pid_t pid = g_stan_sklepu->kasy_samoobslugowe[slot].pid;
    if (pid > 0) {
        kill(pid, SIGUSR1); //Wysylamy prosbe o lagodne zamkniecie
        
        //Czekamy az kasa skonczy obsluge i wyjdzie
        int status;
        waitpid(pid, &status, 0); 
        
        g_stan_sklepu->kasy_samoobslugowe[slot].stan = KASA_ZAMKNIETA;
        g_stan_sklepu->kasy_samoobslugowe[slot].pid = 0;
        if (g_stan_sklepu->liczba_czynnych_kas_samoobslugowych > 0) {
            g_stan_sklepu->liczba_czynnych_kas_samoobslugowych--;
        }
        ZapiszLogF(LOG_INFO, "Zamknieto kase %d", slot + 1);
    }
    ZwolnijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
}

//Watek skalujacy + zbieranie zombie
static void* WatekSkalujacy(void* arg) {
    (void)arg;

    //Blokujemy sygnały w tym watku, zeby SIGTERM trafil do glownego watku
    sigset_t set;
    sigfillset(&set);
    pthread_sigmask(SIG_BLOCK, &set, NULL);
    
    while (1) {
        //Zbieranie zombie (zakonczonych procesow potomnych)
        int status;
        pid_t pid = waitpid(-1, &status, WNOHANG);
        if (pid > 0) { //Ktos lub cos zabilo proces potomny recznie

            pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
            ZajmijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
            for (int i = 0; i < LICZBA_KAS_SAMOOBSLUGOWYCH; i++) {
                if (g_stan_sklepu->kasy_samoobslugowe[i].pid == pid) {
                    g_stan_sklepu->kasy_samoobslugowe[i].stan = KASA_ZAMKNIETA;
                    g_stan_sklepu->kasy_samoobslugowe[i].pid = 0;
                    if (g_stan_sklepu->liczba_czynnych_kas_samoobslugowych > 0) {
                        g_stan_sklepu->liczba_czynnych_kas_samoobslugowych--;
                    }
                    break;
                }
            }
            ZwolnijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
            pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
        }
        
        //Czekamy na sygnal nowego klienta
        if (ZajmijSemafor(g_sem_id, SEM_NOWY_KLIENT) == -1) {
            continue;
        }

        unsigned int liczba_klientow = (unsigned int)PobierzLiczbeKlientow(g_sem_id, g_stan_sklepu->max_klientow_rownoczesnie);
        
        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
        ZajmijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
        unsigned int aktywne = g_stan_sklepu->liczba_czynnych_kas_samoobslugowych;
        ZwolnijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
        
        unsigned int wymagane = ObliczWymaganaLiczbeKas(liczba_klientow);
        
        //Skalowanie w gore - dodawanie kas
        while (aktywne < wymagane && aktywne < LICZBA_KAS_SAMOOBSLUGOWYCH) {
            int wolny_slot = -1;
            
            pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
            ZajmijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
            for (int i = 0; i < LICZBA_KAS_SAMOOBSLUGOWYCH; i++) {
                if (g_stan_sklepu->kasy_samoobslugowe[i].stan == KASA_ZAMKNIETA) {
                    wolny_slot = i;
                    break;
                }
            }
            ZwolnijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
            pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
            
            if (wolny_slot != -1) {
                if (UruchomKase(wolny_slot) > 0) aktywne++;
                else break;
            } else break;
        }

        //Skalowanie w dol - zamykanie kas gdy klientow < K*(N-3)
        if (aktywne > MIN_KAS_SAMO_CZYNNYCH && aktywne > wymagane) {
            unsigned int prog = KLIENCI_NA_KASE * (aktywne - MIN_KAS_SAMO_CZYNNYCH);

            if (liczba_klientow < prog) {
                //Zamykamy od konca
                for (int i = LICZBA_KAS_SAMOOBSLUGOWYCH - 1; i > 0; i--) {
                    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
                    ZajmijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);

                    //Mozna zamknac tylko kase ktora jest WOLNA
                    int mozna_zamknac = (g_stan_sklepu->kasy_samoobslugowe[i].stan == KASA_WOLNA);
                    ZwolnijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
                    
                    if (mozna_zamknac) {
                        ZamknijKase(i);
                        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
                        break;
                    }
                    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
                }
            }
        }
    }
    
    return NULL;
}

//Obsluga SIGTERM
static void ObslugaSIGTERM(int sig) {
    (void)sig;

    if (!g_czy_rodzic) {
        ZapiszLogF(LOG_INFO, "Kasa samoobslugowa [PID: %d]: Ewakuacja (SIGTERM)", getpid());
        if (g_stan_sklepu) OdlaczPamiecWspoldzielona(g_stan_sklepu);
        _exit(0);
    }

    //Rodzic ignoruje kolejne sygnaly
    signal(SIGTERM, SIG_IGN);
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN); //Ignoruj Ctrl+Z
    
    kill(0, SIGTERM);
    
    //Czekamy na zakonczenie wszystkich dzieci
    pid_t pid_wait;
    int status;
    while ((pid_wait = wait(&status)) > 0);

    ZapiszLog(LOG_INFO, "Kasy samoobslugowe: Zakonczono wszystkie procesy potomne.");

    if (g_stan_sklepu) OdlaczPamiecWspoldzielona(g_stan_sklepu);
    ZapiszLog(LOG_INFO, "Kasy samoobslugowe: Zakonczono.");
    _exit(0);
}

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    //Inicjalizacja procesu pochodnego
    if (InicjalizujProcesPochodny(&g_stan_sklepu, &g_sem_id, "Kasa samoobslugowa") == -1) return 1;

    g_msg_id = PobierzIdKolejki(ID_IPC_SAMO);
    if (g_msg_id == -1) {
        ZapiszLog(LOG_BLAD, "Blad pobierania kolejki IPC.");
        return 1;
    }
    
    //Ustawienie grupy procesow
    setpgid(0, 0);
    
    //Obsluga sygnalow
    struct sigaction sa;
    sa.sa_handler = ObslugaSIGTERM;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    ZapiszLog(LOG_INFO, "Kasy samoobslugowe: Start.");

    //Kasa 0 - proces glowny (nie fork)
    ZajmijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    g_stan_sklepu->kasy_samoobslugowe[0].pid = getpid();
    g_stan_sklepu->kasy_samoobslugowe[0].stan = KASA_WOLNA;
    g_stan_sklepu->liczba_czynnych_kas_samoobslugowych = 1;
    ZwolnijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);

    //Uruchomienie poczatkowych kas 1 i 2 (razem 3 poczatkowe)
    for (int i = 1; i < MIN_KAS_SAMO_CZYNNYCH; i++) {
        UruchomKase(i);
    }

    //Uruchomienie watku skalujacego
    pthread_create(&g_watek_skalujacy, NULL, WatekSkalujacy, NULL);

    srand(time(NULL) ^ getpid());
    ZapiszLogF(LOG_INFO, "Kasa samoobslugowa [1]: Uruchomiona (proces glowny, PID: %d).", getpid());

    //Glowna petla managera kasy 0
    int wynik;
    while ((wynik = ObsluzKlienta(0)) > 0);
    
    ZapiszLog(LOG_INFO, "Kasa samoobslugowa [1]: Zakonczona.");
    
    pthread_cancel(g_watek_skalujacy);
    pthread_join(g_watek_skalujacy, NULL);
    
    OdlaczPamiecWspoldzielona(g_stan_sklepu);
    return 0;
}
#endif
