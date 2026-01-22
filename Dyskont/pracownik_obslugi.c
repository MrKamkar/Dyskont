#include "pracownik_obslugi.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>
#include <sys/msg.h>
#include <errno.h>
#include "kolejki.h"

//Wysylanie zadania do pracownika obslugi przez kase samoobslugowa
int WyslijZadanieObslugi(int id_kasy, int typ_operacji, int wiek, int sem_id) {

    int msg_id = PobierzIdKolejki(ID_IPC_PRACOWNIK);
    if (msg_id == -1) {
        return -1;
    }
    
    MsgPracownik msg;
    msg.mtype = MSG_TYPE_PRACOWNIK; //4
    msg.operacja = typ_operacji;
    msg.id_kasy = id_kasy; //ID Kasy jako nadawca
    msg.wiek = wiek;
    
    size_t size = sizeof(MsgPracownik) - sizeof(long);
    
    if (WyslijKomunikat(msg_id, &msg, size, sem_id, SEM_KOLEJKA_PRACOWNIK) == -1) {
        if (errno != EINTR) perror("Pracownik helper msgsnd");
        return -1;
    }
    
    //Czekanie na odpowiedz pracownika
    MsgPracownik res;
    if (OdbierzKomunikat(msg_id, &res, size, MSG_RES_PRACOWNIK_BASE + id_kasy, 0, sem_id, SEM_KOLEJKA_PRACOWNIK) == -1) {
        if (errno != EINTR) perror("Pracownik helper msgrcv");
        return -1;
    }
    
    return res.operacja; //Zwracamy wynik z pola operacja (0 to niepowodzenie, 1 to powodzenie)
}

#ifdef PRACOWNIK_STANDALONE

//Zmienne globalne dla standalone
static StanSklepu* g_stan_sklepu_pracownik = NULL;

static void ObslugaSIGTERM(int sig) {
    (void)sig;
    if (g_stan_sklepu_pracownik) OdlaczPamiecWspoldzielona(g_stan_sklepu_pracownik);
    _exit(0);
}

int main() {
    //Inicjalizacja pamieci wspoldzielonej i semafora
    StanSklepu* stan_sklepu;
    int sem_id;
    
    //Dolaczanie do pamieci wspoldzielonej
    if (InicjalizujProcesPochodny(&stan_sklepu, &sem_id, "Pracownik") == -1) {
        return 1;
    }
    g_stan_sklepu_pracownik = stan_sklepu;
    
    //Obsluga sygnalow wyjscia
    struct sigaction sa_sigterm;
    sa_sigterm.sa_handler = ObslugaSIGTERM;
    sigemptyset(&sa_sigterm.sa_mask);
    sa_sigterm.sa_flags = 0;
    sigaction(SIGTERM, &sa_sigterm, NULL);
    sigaction(SIGQUIT, &sa_sigterm, NULL);
    sigaction(SIGINT, &sa_sigterm, NULL);
    
    ZapiszLog(LOG_INFO, "Pracownik obslugi: Proces uruchomiony, nasluchuje na MSGQ..");
    
    //Dolaczanie do kolejki komunikatow
    int msg_id = PobierzIdKolejki(ID_IPC_PRACOWNIK);
    if (msg_id == -1) {
        ZapiszLog(LOG_BLAD, "Pracownik obslugi: Blad dolaczenia do kolejki komunikatow!");
        OdlaczPamiecWspoldzielona(stan_sklepu);
        return 1;
    }
    
    size_t size = sizeof(MsgPracownik) - sizeof(long);
    MsgPracownik msg_in;
    
    while (1) {
        //Odbieramy tylko typ MSG_TYPE_PRACOWNIK, by nie odebrac wlasnych odpowiedzi
        int odb_msg = OdbierzKomunikat(msg_id, &msg_in, size, MSG_TYPE_PRACOWNIK, 0, sem_id, SEM_KOLEJKA_PRACOWNIK);
        
        if (odb_msg != -1) {
            int id_kasy_nadawcy = msg_in.id_kasy;
            int operacja = msg_in.operacja;
            int wynik = 1; //Domyslnie powodzenie
            
            //Symulacja czasu reakcji
            SYMULACJA_USLEEP(stan_sklepu, 500000); //0.5s
            
            if (operacja == OP_WERYFIKACJA_WIEKU) {
                if (msg_in.wiek < 18) {
                    ZapiszLogF(LOG_INFO, "Pracownik: Weryfikacja wieku NIEUDANA (lat: %d) dla kasy %d", msg_in.wiek, id_kasy_nadawcy + 1);
                    wynik = 0;
                } else ZapiszLogF(LOG_INFO, "Pracownik: Weryfikacja wieku OK (lat: %d) dla kasy %d", msg_in.wiek, id_kasy_nadawcy + 1);
            } else if (operacja == OP_ODBLOKOWANIE_KASY) ZapiszLogF(LOG_INFO, "Pracownik: Odblokowanie kasy %d", id_kasy_nadawcy + 1);
            
            //Odeslij wynik (VIP, by nie zablokowalo sie przy duzej liczbie klientow)
            MsgPracownik res;
            res.mtype = MSG_RES_PRACOWNIK_BASE + id_kasy_nadawcy;
            res.operacja = wynik;
            res.id_kasy = id_kasy_nadawcy;
            
            WyslijKomunikatVIP(msg_id, &res, size);
            
        } else {
            if (errno == EINTR) continue;
            //Inny blad
            perror("Pracownik: msgrcv");
            break; 
        }
    }
    
    OdlaczPamiecWspoldzielona(stan_sklepu);
    return 0;
}
#endif
