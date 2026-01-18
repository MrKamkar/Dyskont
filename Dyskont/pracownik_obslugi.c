#include "pracownik_obslugi.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/msg.h>
#include <errno.h>

//Wysylanie zadania do pracownika obslugi przez kase samoobslugowa
int WyslijZadanieObslugi(int id_kasy, int typ_operacji, int wiek) {
    int msg_id = msgget(GenerujKluczIPC(ID_IPC_KOLEJKA), 0600);
    if (msg_id == -1) {
        return -1;
    }
    
    Komunikat msg;
    msg.mtype = MSG_TYPE_PRACOWNIK; //4
    msg.operacja = typ_operacji;
    msg.liczba_produktow = id_kasy; //ID Kasy jako nadawca
    msg.wiek = wiek;
    msg.id_klienta = 0;
    msg.suma_koszyka = 0.0;
    msg.ma_alkohol = 0;
    
    size_t size = sizeof(Komunikat) - sizeof(long);
    
    //Wyslij do kolejki komunikatow
    if (msgsnd(msg_id, &msg, size, 0) == -1) {
        if (errno != EINTR) perror("Pracownik helper msgsnd");
        return -1;
    }
    
    //Czekanie na odpowiedz pracownika
    Komunikat res;
    if (msgrcv(msg_id, &res, size, MSG_RES_PRACOWNIK_BASE + id_kasy, 0) == -1) {
        if (errno != EINTR) perror("Pracownik helper msgrcv");
        return -1;
    }
    
    return res.operacja; //Zwracamy wynik z pola operacja (0 to niepowodzenie, 1 to powodzenie)
}

#ifdef PRACOWNIK_STANDALONE
int main() {
    //Inicjalizacja pamieci wspoldzielonej i semafora
    StanSklepu* stan_sklepu;
    int sem_id;
    
    //Dolaczanie do pamieci wspoldzielonej
    if (InicjalizujProcesPochodny(&stan_sklepu, &sem_id, "Pracownik") == -1) {
        return 1;
    }
    
    //Obsluga sygnalow
    struct sigaction sa;
    sa.sa_handler = ObslugaSygnaluWyjscia;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL); 
    
    ZapiszLog(LOG_INFO, "Pracownik obslugi: Proces uruchomiony, nasluchuje na MSGQ...");
    
    //Dolaczanie do kolejki komunikatow
    int msg_id = msgget(GenerujKluczIPC(ID_IPC_KOLEJKA), 0600);
    if (msg_id == -1) {
        ZapiszLog(LOG_BLAD, "Pracownik obslugi: Blad dolaczenia do kolejki komunikatow!");
        OdlaczPamiecWspoldzielona(stan_sklepu);
        return 1;
    }
    
    size_t size = sizeof(Komunikat) - sizeof(long);
    
    while (1) {
        //Sprawdz czy koniec symulacji
        if (CZY_KONCZYC(stan_sklepu)) {
             ZapiszLog(LOG_INFO, "Pracownik obslugi: Koniec pracy.");
             break;
        }
        
        Komunikat msg_in;
        //Odbior zlecen od kas samoobslugowych
        if (msgrcv(msg_id, &msg_in, size, MSG_TYPE_PRACOWNIK, 0) != -1) {
            
            int id_kasy_nadawcy = msg_in.liczba_produktow;
            int operacja = msg_in.operacja;
            int wynik = 1; //Domyslnie powodzenie
            
            //Symulacja czasu reakcji
            SYMULACJA_USLEEP(stan_sklepu, 500000); //0.5s
            
            if (operacja == OP_WERYFIKACJA_WIEKU) {
                if (msg_in.wiek < 18) {
                    ZapiszLogF(LOG_INFO, "Pracownik: Weryfikacja wieku NIEUDANA (lat: %d) dla kasy %d", msg_in.wiek, id_kasy_nadawcy + 1);
                    wynik = 0;
                } else {
                    ZapiszLogF(LOG_INFO, "Pracownik: Weryfikacja wieku OK (lat: %d) dla kasy %d", msg_in.wiek, id_kasy_nadawcy + 1);
                    wynik = 1;
                }
            } else if (operacja == OP_ODBLOKOWANIE_KASY) {
                ZapiszLogF(LOG_INFO, "Pracownik: Odblokowanie kasy %d", id_kasy_nadawcy + 1);
                wynik = 1;
            }
            
            //Odeslij wynik czy mozna bylo odblokowac kase samoobslugowa
            Komunikat res;
            res.mtype = MSG_RES_PRACOWNIK_BASE + id_kasy_nadawcy;
            res.operacja = wynik;
            res.liczba_produktow = id_kasy_nadawcy;

            //Reszta niepotrzebnych pol
            res.id_klienta = 0;
            res.suma_koszyka = 0;
            res.ma_alkohol = 0;
            res.wiek = 0;
            
            msgsnd(msg_id, &res, size, 0);
            
        } else {
            if (errno == EINTR) continue;

            //Inny blad
            perror("Pracownik msgrcv");
            break; 
        }
    }
    
    OdlaczPamiecWspoldzielona(stan_sklepu);
    return 0;
}
#endif
