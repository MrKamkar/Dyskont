#include "kasjer.h"
#include "wspolne.h"
#include "kolejki.h"
#include <string.h>
#include <signal.h>
#include <sys/msg.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

//Czas skasowania jednego produktu
#define CZAS_SKASOWANIA_PRODUKTU_MS 1000

#ifdef KASJER_STANDALONE

//Zmienne globalne dla obslugi sygnalow
static StanSklepu* g_stan_sklepu = NULL;
static int g_sem_id = -1;
static int g_msg_id = -1;
static int g_id_kasy = -1;

//Handler SIGTERM
static void ObslugaSIGTERM(int sig) {
    (void)sig;
    if (g_stan_sklepu) OdlaczPamiecWspoldzielona(g_stan_sklepu);
    _exit(0);
}

//Obsluga klienta przez kasjera
static void ObsluzKlienta(int id_klienta, unsigned int liczba_produktow, double suma) {
    ZapiszLogF(LOG_INFO, "Kasjer [Kasa %d]: Rozpoczynam obsluge klienta [ID: %d], produktow: %u",
            g_id_kasy + 1, id_klienta, liczba_produktow);
    
    //Symulacja skanowania produktow
    for (unsigned int i = 0; i < liczba_produktow; i++) {
        SYMULACJA_USLEEP(g_stan_sklepu, CZAS_SKASOWANIA_PRODUKTU_MS * 1000);
    }
    
    ZapiszLogF(LOG_INFO, "Kasjer [Kasa %d]: Zakonczono obsluge klienta [ID: %d]. Suma: %.2f PLN",
            g_id_kasy + 1, id_klienta, suma);
}


int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uzycie: %s <id_kasy>\n", argv[0]);
        return 1;
    }
    
    g_id_kasy = atoi(argv[1]);
    
    //Sprawdzenie poprawnosci ID kasy
    if (g_id_kasy < 0 || g_id_kasy >= LICZBA_KAS_STACJONARNYCH) {
        fprintf(stderr, "Nieprawidlowy ID kasy: %d (dozwolone: 0-%d)\n", 
                g_id_kasy, LICZBA_KAS_STACJONARNYCH - 1);
        return 1;
    }
    
    //Inicjalizacja procesu pochodnego
    if (InicjalizujProcesPochodny(&g_stan_sklepu, &g_sem_id, "Kasjer") == -1) {
        return 1;
    }
    
    //Dolaczenie do odpowiedniej kolejki komunikatow
    char id_projektu = (g_id_kasy == 0) ? ID_IPC_KASA_1 : ID_IPC_KASA_2;
    g_msg_id = PobierzIdKolejki(id_projektu);
    
    if (g_msg_id == -1) {
        ZapiszLogF(LOG_BLAD, "Kasjer [Kasa %d]: Blad dolaczenia do kolejki komunikatow!", g_id_kasy + 1);
        OdlaczPamiecWspoldzielona(g_stan_sklepu);
        return 1;
    }
    
    //Obsluga sygnalow
    struct sigaction sa_sigterm;
    sa_sigterm.sa_handler = ObslugaSIGTERM;
    sigemptyset(&sa_sigterm.sa_mask);
    sa_sigterm.sa_flags = 0;
    sigaction(SIGTERM, &sa_sigterm, NULL);
    sigaction(SIGQUIT, &sa_sigterm, NULL);
    sigaction(SIGINT, &sa_sigterm, NULL);

    ZapiszLogF(LOG_INFO, "Kasjer [Kasa %d]: Uruchomiony (PID: %d).", g_id_kasy + 1, getpid());

    //Ustawienie kasy jako wolna
    ZajmijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    g_stan_sklepu->kasy_stacjonarne[g_id_kasy].stan = KASA_WOLNA;
    g_stan_sklepu->kasy_stacjonarne[g_id_kasy].pid = getpid();
    ZwolnijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);

    //Glowna petla kasjera
    while (1) {
        MsgKasaStacj msg_in;
        size_t msg_size = sizeof(MsgKasaStacj) - sizeof(long);
        
        //Odbior blokujacy - czekamy na klienta
        int res = OdbierzKomunikat(g_msg_id, &msg_in, msg_size, 0, 0);
        
        if (res != -1) {
            int id_klienta = msg_in.id_klienta;
            unsigned int liczba_produktow = msg_in.liczba_produktow;
            double suma = msg_in.suma_koszyka;
            
            //Zaktualizuj stan kasy na ZAJETA
            ZajmijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
            g_stan_sklepu->kasy_stacjonarne[g_id_kasy].stan = KASA_ZAJETA;
            g_stan_sklepu->kasy_stacjonarne[g_id_kasy].id_klienta = id_klienta;
            ZwolnijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
            
            //Obsluga klienta
            ObsluzKlienta(id_klienta, liczba_produktow, suma);
            
            //Wyslij potwierdzenie do klienta (VIP)
            MsgKasaStacj msg_out;
            msg_out.mtype = MSG_RES_STACJONARNA_BASE + id_klienta;
            msg_out.id_klienta = id_klienta;
            msg_out.liczba_produktow = g_id_kasy; //Zwracamy ID kasy
            WyslijKomunikatVIP(g_sem_id, g_msg_id, &msg_out, msg_size);
            
            //Zwolnij kase
            ZajmijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
            g_stan_sklepu->kasy_stacjonarne[g_id_kasy].stan = KASA_WOLNA;
            g_stan_sklepu->kasy_stacjonarne[g_id_kasy].id_klienta = -1;
            ZwolnijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
            
        } else {
            if (errno == EINTR) {
                //Przerwany sygnalem - konczymy
                break;
            }
            ZapiszLogF(LOG_BLAD, "Kasjer [Kasa %d]: Blad msgrcv (errno=%d)", g_id_kasy + 1, errno);
            break;
        }
    }

    ZapiszLogF(LOG_INFO, "Kasjer [Kasa %d]: Zakonczony.", g_id_kasy + 1);
    OdlaczPamiecWspoldzielona(g_stan_sklepu);
    return 0;
}
#endif