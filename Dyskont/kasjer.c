#include "kasjer.h"
#include "pamiec_wspoldzielona.h"
#include "kolejki.h"
#include <string.h>
#include <signal.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#ifdef KASJER_STANDALONE

//Zmienne globalne dla obslugi sygnalow
static StanSklepu* g_stan_sklepu = NULL;
static int g_sem_id = -1;
static int g_msg_id_wspolna = -1;
static int g_msg_id_1 = -1;
static int g_msg_id_2 = -1;
static int g_czy_rodzic = 1;
static pthread_t g_watek_zarzadzajacy;
static volatile sig_atomic_t g_timeout = 0;
static volatile sig_atomic_t g_zamykanie = 0;

//Sprawdza czy klient zostal zmigrowany z kasy1 do kasy2 (uzywa pamieci wspoldzielonej)
//UWAGA: Szuka POZYTYWNEGO id_klienta (odroznienie od samoobslugowej ktora uzywa negatywnych)
static int CzyZmigrowany(int id_klienta) {
    ZajmijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    int wynik = 0;
    unsigned int max = g_stan_sklepu->max_klientow_rownoczesnie;
    for (unsigned int i = 0; i < max; i++) {
        if (g_stan_sklepu->pomijani_klienci[i] == id_klienta) {
            //Usun z tablicy (juz nie potrzebujemy)
            g_stan_sklepu->pomijani_klienci[i] = 0;
            wynik = 1;
            break;
        }
    }
    ZwolnijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    return wynik;
}

//Dodaje klienta do tablicy zmigrowanych (migracja kasa1->kasa2)
//UWAGA: Zapisuje POZYTYWNE id_klienta (odroznienie od samoobslugowej)
static void DodajZmigrowanego(int id_klienta) {
    ZajmijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    unsigned int max = g_stan_sklepu->max_klientow_rownoczesnie;
    for (unsigned int i = 0; i < max; i++) {
        if (g_stan_sklepu->pomijani_klienci[i] == 0) {
            g_stan_sklepu->pomijani_klienci[i] = id_klienta;
            break;
        }
    }
    ZwolnijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
}

//Handler SIGTERM
static void ObslugaSIGTERM(int sig) {
    (void)sig;

    if (!g_czy_rodzic) {
        if (g_stan_sklepu) OdlaczPamiecWspoldzielona(g_stan_sklepu);
        _exit(0);
    }

    //Rodzic ignoruje kolejne sygnaly
    signal(SIGTERM, SIG_IGN);
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    
    g_zamykanie = 1;
    
    kill(0, SIGTERM);
    
    //Czekamy na zakonczenie wszystkich dzieci
    pid_t pid_wait;
    int status;
    while ((pid_wait = wait(&status)) > 0) {
        if (WIFEXITED(status)) {
            int exit_code = WEXITSTATUS(status);
            ZapiszLogF(LOG_INFO, "Kasa stacjonarna [PID: %d] zakonczona (status: %d)", pid_wait, exit_code);
        } else if (WIFSIGNALED(status)) {
            ZapiszLogF(LOG_INFO, "Kasa stacjonarna [PID: %d] zabita sygnalem %d", pid_wait, WTERMSIG(status));
        }
    }

    ZapiszLog(LOG_INFO, "Kasy stacjonarne: Zakonczono wszystkie procesy potomne.");

    if (g_stan_sklepu) OdlaczPamiecWspoldzielona(g_stan_sklepu);
    ZapiszLog(LOG_INFO, "Kasy stacjonarne: Zakonczono.");
    _exit(0);
}

//Handler SIGALRM - timeout bezczynnosci
static void ObslugaSIGALRM(int sig) {
    (void)sig;
    g_timeout = 1;
}

//Forward declarations dla handlerów sygnałów
static pid_t UruchomKase2();
static void ZamknijKase(int id_kasy);

//Handler SIGUSR1 - otwieranie kasy 2
static void ObslugaSIGUSR1(int sig) {
    (void)sig;
    
    if (!g_stan_sklepu || !g_czy_rodzic) return;
    
    ZajmijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    StanKasy stan = g_stan_sklepu->kasy_stacjonarne[1].stan;
    ZwolnijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    
    if (stan == KASA_ZAMKNIETA) {
        UruchomKase2();
        ZapiszLog(LOG_INFO, "Sygnal SIGUSR1: Otwarto kase 2.");
    } else {
        ZapiszLog(LOG_OSTRZEZENIE, "Sygnal SIGUSR1: Kasa 2 jest juz otwarta.");
    }
}

//Handler SIGUSR2 - zamykanie kasy (1 lub 2 wg id_kasy_do_zamkniecia)
static void ObslugaSIGUSR2(int sig) {
    (void)sig;
    
    if (!g_stan_sklepu || !g_czy_rodzic) return;
    
    ZajmijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    int id_kasy = g_stan_sklepu->id_kasy_do_zamkniecia;
    ZwolnijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    
    if (id_kasy == 0 || id_kasy == 1) {
        ZamknijKase(id_kasy);
        ZapiszLogF(LOG_INFO, "Sygnal SIGUSR2: Zamykanie kasy %d.", id_kasy + 1);
    }
}

//Obsluga klienta przez kasjera
static void ObsluzKlientaStacjonarnie(int id_kasy, int id_klienta, unsigned int liczba_produktow, double suma) {
    ZapiszLogF(LOG_INFO, "Kasjer [Kasa %d]: Rozpoczynam obsluge klienta [ID: %d], produktow: %u",
            id_kasy + 1, id_klienta, liczba_produktow);
    
    //Symulacja skanowania produktow
    for (unsigned int i = 0; i < liczba_produktow; i++) {
        SYMULACJA_USLEEP(g_stan_sklepu, CZAS_SKASOWANIA_PRODUKTU_MS * 1000);
    }
    
    ZapiszLogF(LOG_INFO, "Kasjer [Kasa %d]: Zakonczono obsluge klienta [ID: %d]. Suma: %.2f PLN",
            id_kasy + 1, id_klienta, suma);
}

//Obsługa jednego klienta z kolejki (dla kasy 1 - sprawdza migracje)
static int ObsluzKlientaKasa1(int msg_id) {
    MsgKasaStacj msg_in;
    size_t msg_size = sizeof(MsgKasaStacj) - sizeof(long);
    
    g_timeout = 0;
    alarm(TIMEOUT_BEZCZYNNOSCI_S);
    
    //Odbior blokujacy - czekamy na klienta
    int res = OdbierzKomunikat(msg_id, &msg_in, msg_size, 0, 0, g_sem_id, SEM_KOLEJKA_KASA_1);
    
    alarm(0);
    
    if (res != -1) {
        int id_klienta = msg_in.id_klienta;
        
        //Sprawdz czy klient zostal zmigrowany do kasy 2
        if (CzyZmigrowany(id_klienta)) {
            ZapiszLogF(LOG_INFO, "Kasa 1: Pomijam klienta [ID: %d] - zmigrowany do kasy 2.", id_klienta);
            return 1; //Kontynuuj - pomiń tego klienta
        }
        
        unsigned int liczba_produktow = msg_in.liczba_produktow;
        double suma = msg_in.suma_koszyka;
        
        //Zaktualizuj stan kasy na ZAJETA
        //ZapiszLog(LOG_DEBUG, "Kasjer 1: Zajmuje mutex (stan ZAJETA)");
        ZajmijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
        g_stan_sklepu->kasy_stacjonarne[0].stan = KASA_ZAJETA;
        g_stan_sklepu->kasy_stacjonarne[0].id_klienta = id_klienta;
        ZwolnijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
        //ZapiszLog(LOG_DEBUG, "Kasjer 1: Zwolniono mutex (stan ZAJETA)");
        
        //Obsluga klienta
        ObsluzKlientaStacjonarnie(0, id_klienta, liczba_produktow, suma);
        
        //Wyslij potwierdzenie do klienta przez WSPOLNA kolejke
        MsgKasaStacj msg_out;
        msg_out.mtype = MSG_RES_STACJONARNA_BASE + id_klienta;
        msg_out.id_klienta = id_klienta;
        msg_out.liczba_produktow = 0; //ID kasy = 0
        WyslijKomunikatVIP(g_msg_id_wspolna, &msg_out, msg_size);
        
        //Zwolnij kase
        //ZapiszLog(LOG_DEBUG, "Kasjer 1: Zajmuje mutex (stan WOLNA)");
        ZajmijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
        g_stan_sklepu->kasy_stacjonarne[0].stan = KASA_WOLNA;
        g_stan_sklepu->kasy_stacjonarne[0].id_klienta = -1;
        ZwolnijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
        //ZapiszLog(LOG_DEBUG, "Kasjer 1: Zwolniono mutex (stan WOLNA)");
        
        return 1; //Sukces - kontynuuj
    } else {
        if (errno == EINTR && g_timeout) {
            return 0; //Timeout - zamknij kase
        }
        if (errno == EINTR) {
            return -1; //Przerwany sygnalem - konczymy
        }
        ZapiszLogF(LOG_BLAD, "Kasjer [Kasa 1]: Blad msgrcv (errno=%d)", errno);
        return -1;
    }
}

//Obsługa jednego klienta z kolejki dla kasy 2
static int ObsluzKlientaKasa2(int msg_id) {
    MsgKasaStacj msg_in;
    size_t msg_size = sizeof(MsgKasaStacj) - sizeof(long);
    
    g_timeout = 0;
    alarm(TIMEOUT_BEZCZYNNOSCI_S);
    
    //Odbior blokujacy - czekamy na klienta
    int res = OdbierzKomunikat(msg_id, &msg_in, msg_size, 0, 0, g_sem_id, SEM_KOLEJKA_KASA_2);
    
    alarm(0);
    
    if (res != -1) {
        int id_klienta = msg_in.id_klienta;
        unsigned int liczba_produktow = msg_in.liczba_produktow;
        double suma = msg_in.suma_koszyka;
        
        //Zaktualizuj stan kasy na ZAJETA
        ZajmijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
        g_stan_sklepu->kasy_stacjonarne[1].stan = KASA_ZAJETA;
        g_stan_sklepu->kasy_stacjonarne[1].id_klienta = id_klienta;
        ZwolnijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
        
        //Obsluga klienta
        ObsluzKlientaStacjonarnie(1, id_klienta, liczba_produktow, suma);
        
        //Wyslij potwierdzenie do klienta przez WSPOLNA kolejke
        MsgKasaStacj msg_out;
        msg_out.mtype = MSG_RES_STACJONARNA_BASE + id_klienta;
        msg_out.id_klienta = id_klienta;
        msg_out.liczba_produktow = 1; //ID kasy = 1
        WyslijKomunikatVIP(g_msg_id_wspolna, &msg_out, msg_size);
        
        //Zwolnij kase
        ZajmijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
        g_stan_sklepu->kasy_stacjonarne[1].stan = KASA_WOLNA;
        g_stan_sklepu->kasy_stacjonarne[1].id_klienta = -1;
        ZwolnijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
        
        return 1; //Sukces - kontynuuj
    } else {
        if (errno == EINTR && g_timeout) {
            return 0; //Timeout - zamknij kase
        }
        if (errno == EINTR) {
            return -1; //Przerwany sygnalem - konczymy
        }
        ZapiszLogF(LOG_BLAD, "Kasjer [Kasa 2]: Blad msgrcv (errno=%d)", errno);
        return -1;
    }
}

//Logika procesu potomnego (Kasa 2)
static void LogikaKasy2() {
    srand(time(NULL) ^ getpid());
    ZapiszLogF(LOG_INFO, "Kasa stacjonarna 2: Uruchomiona (PID: %d).", getpid());
    
    int wynik;
    while ((wynik = ObsluzKlientaKasa2(g_msg_id_2)) > 0);
    
    if (wynik == 0) {
        ZapiszLog(LOG_INFO, "Kasa 2: Timeout 30s - zamykanie.");
    }
    
    //Oznacz jako zamknieta
    ZajmijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    g_stan_sklepu->kasy_stacjonarne[1].stan = KASA_ZAMKNIETA;
    g_stan_sklepu->kasy_stacjonarne[1].pid = 0;
    ZwolnijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    
    ZapiszLog(LOG_INFO, "Kasa stacjonarna 2: Zakonczona.");
}

//Uruchomienie kasy 2 (fork)
static pid_t UruchomKase2() {
    pid_t pid = fork();
    
    if (pid == 0) {
        //Dziecko ma miec odblokowane sygnaly
        sigset_t set;
        sigemptyset(&set);
        pthread_sigmask(SIG_SETMASK, &set, NULL);
        
        g_czy_rodzic = 0;
        LogikaKasy2();
        OdlaczPamiecWspoldzielona(g_stan_sklepu);
        exit(0);
    } else if (pid > 0) {
        ZajmijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
        g_stan_sklepu->kasy_stacjonarne[1].pid = pid;
        g_stan_sklepu->kasy_stacjonarne[1].stan = KASA_WOLNA;
        ZwolnijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
        
        ZapiszLogF(LOG_INFO, "Uruchomiono kase stacjonarna 2 [PID: %d]", pid);
    }
    
    return pid;
}

//Zamkniecie kasy (oznaczenie jako ZAMYKANA)
static void ZamknijKase(int id_kasy) {
    if (id_kasy < 0 || id_kasy >= LICZBA_KAS_STACJONARNYCH) return;
    
    ZajmijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    StanKasy stan = g_stan_sklepu->kasy_stacjonarne[id_kasy].stan;
    if (stan != KASA_ZAMKNIETA && stan != KASA_ZAMYKANA) {
        g_stan_sklepu->kasy_stacjonarne[id_kasy].stan = KASA_ZAMYKANA;
        ZwolnijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
        
        ZapiszLogF(LOG_INFO, "Kasa %d: Oznaczona jako ZAMYKANA - obsluzy reszte kolejki.", id_kasy + 1);
    } else {
        ZwolnijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    }
}

//Przeniesienie klientow ze wspolnej kolejki do kasy 1
static void MigrujZWspolnejDoKasy1() {
    MsgKasaStacj msg;
    size_t msg_size = sizeof(MsgKasaStacj) - sizeof(long);
    
    int przeniesiono = 0;
    while (OdbierzKomunikat(g_msg_id_wspolna, &msg, msg_size, MSG_TYPE_KASA_WSPOLNA, IPC_NOWAIT, g_sem_id, SEM_KOLEJKA_WSPOLNA) == 0) {
        msg.mtype = MSG_TYPE_KASA_1;
        if (WyslijKomunikat(g_msg_id_1, &msg, msg_size, g_sem_id, SEM_KOLEJKA_KASA_1) == 0) {
            przeniesiono++;
        }
    }
    
    if (przeniesiono > 0) {
        ZapiszLogF(LOG_INFO, "Przeniesiono %d klientow ze wspolnej kolejki do kasy 1.", przeniesiono);
    }
}

//Przeniesienie jednego klienta z kasy 1 do kasy 2 (dodaje do tablicy zmigrowanych)
static void MigrujJednegoKlienta() {
    MsgKasaStacj msg;
    size_t msg_size = sizeof(MsgKasaStacj) - sizeof(long);
    
    if (OdbierzKomunikat(g_msg_id_1, &msg, msg_size, 0, IPC_NOWAIT, g_sem_id, SEM_KOLEJKA_KASA_1) == 0) {
        //Dodaj do tablicy zmigrowanych - kasa 1 pominie tego klienta
        DodajZmigrowanego(msg.id_klienta);
        
        //Wyslij do kasy 2
        msg.mtype = MSG_TYPE_KASA_2;
        if (WyslijKomunikat(g_msg_id_2, &msg, msg_size, g_sem_id, SEM_KOLEJKA_KASA_2) == 0) {
            ZapiszLogF(LOG_INFO, "Migracja klienta [ID: %d] z kasy 1 do kasy 2.", msg.id_klienta);
        }
    }
}

//Watek zarzadzajacy (routing + migracja)
static void* WatekZarzadzajacy(void* arg) {
    (void)arg;
    
    //Blokujemy sygnaly w tym watku, zeby SIGTERM trafil do glownego watku
    sigset_t set;
    sigfillset(&set);
    pthread_sigmask(SIG_BLOCK, &set, NULL);
    
    while (!g_zamykanie) {
        //Zbieranie zombie (kasa 2)
        int status;
        pid_t pid = waitpid(-1, &status, WNOHANG);
        if (pid > 0) {
            pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
            ZajmijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
            if (g_stan_sklepu->kasy_stacjonarne[1].pid == pid) {
                g_stan_sklepu->kasy_stacjonarne[1].stan = KASA_ZAMKNIETA;
                g_stan_sklepu->kasy_stacjonarne[1].pid = 0;
            }
            ZwolnijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
            pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
        }
        
        //Sprawdz rozmiar wspolnej kolejki
        int rozmiar_wspolnej = PobierzRozmiarKolejki(g_msg_id_wspolna);
        
        //Pobierz stan kas
        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
        ZajmijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
        StanKasy stan_kasy_1 = g_stan_sklepu->kasy_stacjonarne[0].stan;
        StanKasy stan_kasy_2 = g_stan_sklepu->kasy_stacjonarne[1].stan;
        ZwolnijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
        
        //Automatyczne otwieranie kasy 1
        if (stan_kasy_1 == KASA_ZAMKNIETA && rozmiar_wspolnej > PROG_OTWARCIA_KASY_1) {
            pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
            ZajmijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
            g_stan_sklepu->kasy_stacjonarne[0].stan = KASA_WOLNA;
            ZwolnijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
            pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
            
            ZapiszLog(LOG_INFO, "Automatyczne otwarcie kasy 1 (kolejka > 3).");
            
            //Przenies klientow ze wspolnej do kasy 1
            MigrujZWspolnejDoKasy1();
        }
        
        //Routing: przekieruj klientow ze wspolnej kolejki do otwartych kas
        int k1_aktywna = (stan_kasy_1 != KASA_ZAMKNIETA && stan_kasy_1 != KASA_ZAMYKANA);
        int k2_aktywna = (stan_kasy_2 != KASA_ZAMKNIETA && stan_kasy_2 != KASA_ZAMYKANA);

        if (k1_aktywna || k2_aktywna) {
            MsgKasaStacj msg;
            size_t msg_size = sizeof(MsgKasaStacj) - sizeof(long);
            
            //Uzywamy OdbierzKomunikat zeby miec miejsce w kolejce
            if (OdbierzKomunikat(g_msg_id_wspolna, &msg, msg_size, MSG_TYPE_KASA_WSPOLNA, IPC_NOWAIT, g_sem_id, SEM_KOLEJKA_WSPOLNA) == 0) {
                //Wybierz krotsza kolejke (jesli obie aktywne)
                int msg_id_docelowy;
                int sem_kolejki_docelowy;

                if (k1_aktywna && k2_aktywna) {
                    //Obie aktywne - porownaj
                    int rozmiar_1 = PobierzRozmiarKolejki(g_msg_id_1);
                    int rozmiar_2 = PobierzRozmiarKolejki(g_msg_id_2);
                    
                    if (rozmiar_2 >= 0 && rozmiar_2 < rozmiar_1) {
                         msg_id_docelowy = g_msg_id_2;
                         sem_kolejki_docelowy = SEM_KOLEJKA_KASA_2;
                         msg.mtype = MSG_TYPE_KASA_2;
                    } else {
                         msg_id_docelowy = g_msg_id_1;
                         sem_kolejki_docelowy = SEM_KOLEJKA_KASA_1;
                         msg.mtype = MSG_TYPE_KASA_1;
                    }
                } else if (k1_aktywna) {
                    //Tylko 1 aktywna
                    msg_id_docelowy = g_msg_id_1;
                    sem_kolejki_docelowy = SEM_KOLEJKA_KASA_1;
                    msg.mtype = MSG_TYPE_KASA_1;
                } else {
                    //Tylko 2 aktywna
                    msg_id_docelowy = g_msg_id_2;
                    sem_kolejki_docelowy = SEM_KOLEJKA_KASA_2;
                    msg.mtype = MSG_TYPE_KASA_2;
                }
                
                WyslijKomunikat(msg_id_docelowy, &msg, msg_size, g_sem_id, sem_kolejki_docelowy);
                
                //TERAZ zwalniamy semafor - klient opuscil kolejke wspolna i trafil do kasy X
                ZwolnijSemafor(g_sem_id, SEM_KOLEJKA_WSPOLNA);

                ZapiszLogF(LOG_INFO, "Przekierowano klienta [ID: %d] do kasy %d.",   
                          msg.id_klienta, msg_id_docelowy == g_msg_id_1 ? 1 : 2);
            }
        }
        
        //Migracja z kasy 1 do kasy 2 (jesli 2 krotsza)
        if (stan_kasy_2 != KASA_ZAMKNIETA && stan_kasy_2 != KASA_ZAMYKANA) {
            int rozmiar_1 = PobierzRozmiarKolejki(g_msg_id_1);
            int rozmiar_2 = PobierzRozmiarKolejki(g_msg_id_2);
            
            if (rozmiar_1 > rozmiar_2 + 1) {
                MigrujJednegoKlienta();
            }
        }
        
        usleep(100000); //100ms
    }
    
    return NULL;
}


int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    
    //Inicjalizacja procesu pochodnego
    if (InicjalizujProcesPochodny(&g_stan_sklepu, &g_sem_id, "Kasjer") == -1) {
        return 1;
    }
    
    //Dolaczenie do kolejek komunikatow
    g_msg_id_wspolna = PobierzIdKolejki(ID_IPC_KASA_WSPOLNA);
    g_msg_id_1 = PobierzIdKolejki(ID_IPC_KASA_1);
    g_msg_id_2 = PobierzIdKolejki(ID_IPC_KASA_2);
    
    if (g_msg_id_wspolna == -1 || g_msg_id_1 == -1 || g_msg_id_2 == -1) {
        ZapiszLog(LOG_BLAD, "Kasjer: Blad dolaczenia do kolejek komunikatow!");
        OdlaczPamiecWspoldzielona(g_stan_sklepu);
        return 1;
    }
    
    //Ustawienie grupy procesow
    setpgid(0, 0);
    
    //Obsluga sygnalow
    struct sigaction sa_sigterm;
    sa_sigterm.sa_handler = ObslugaSIGTERM;
    sigemptyset(&sa_sigterm.sa_mask);
    sa_sigterm.sa_flags = 0;
    sigaction(SIGTERM, &sa_sigterm, NULL);
    sigaction(SIGQUIT, &sa_sigterm, NULL);
    sigaction(SIGINT, &sa_sigterm, NULL);
    
    struct sigaction sa_sigalrm;
    sa_sigalrm.sa_handler = ObslugaSIGALRM;
    sigemptyset(&sa_sigalrm.sa_mask);
    sa_sigalrm.sa_flags = 0;
    sigaction(SIGALRM, &sa_sigalrm, NULL);
    
    //Obsluga SIGUSR1 (otwieranie kasy 2) i SIGUSR2 (zamykanie kas)
    struct sigaction sa_sigusr1;
    sa_sigusr1.sa_handler = ObslugaSIGUSR1;
    sigemptyset(&sa_sigusr1.sa_mask);
    sa_sigusr1.sa_flags = 0;
    sigaction(SIGUSR1, &sa_sigusr1, NULL);
    
    struct sigaction sa_sigusr2;
    sa_sigusr2.sa_handler = ObslugaSIGUSR2;
    sigemptyset(&sa_sigusr2.sa_mask);
    sa_sigusr2.sa_flags = 0;
    sigaction(SIGUSR2, &sa_sigusr2, NULL);

    ZapiszLog(LOG_INFO, "Kasy stacjonarne: Start.");

    //Kasa 1 - proces glowny (zamknieta na start)
    ZajmijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    g_stan_sklepu->kasy_stacjonarne[0].pid = getpid();
    g_stan_sklepu->kasy_stacjonarne[0].stan = KASA_ZAMKNIETA;
    g_stan_sklepu->kasy_stacjonarne[1].stan = KASA_ZAMKNIETA;
    g_stan_sklepu->kasy_stacjonarne[1].pid = 0;
    ZwolnijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);

    //Uruchomienie watku zarzadzajacego
    pthread_create(&g_watek_zarzadzajacy, NULL, WatekZarzadzajacy, NULL);

    srand(time(NULL) ^ getpid());
    ZapiszLogF(LOG_INFO, "Kasa stacjonarna [1]: Uruchomiona (proces glowny, PID: %d).", getpid());

    //Glowna petla managera - obsluga kasy 1
    int wynik;
    while ((wynik = ObsluzKlientaKasa1(g_msg_id_1)) >= 0) {
        if (wynik == 0) {
            //Timeout
            ZajmijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
            StanKasy aktualny_stan = g_stan_sklepu->kasy_stacjonarne[0].stan;
            
            if (aktualny_stan != KASA_ZAMKNIETA) {
                g_stan_sklepu->kasy_stacjonarne[0].stan = KASA_ZAMKNIETA;
                ZapiszLog(LOG_INFO, "Kasa 1: Timeout 30s - zamknieta.");
            }
            ZwolnijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
        }
    }
    
    ZapiszLog(LOG_INFO, "Kasa stacjonarna [1]: Zakonczona.");
    
    pthread_cancel(g_watek_zarzadzajacy);
    pthread_join(g_watek_zarzadzajacy, NULL);
    
    OdlaczPamiecWspoldzielona(g_stan_sklepu);
    return 0;
}
#endif
