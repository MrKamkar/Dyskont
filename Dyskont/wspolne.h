#ifndef WSPOLNE_H
#define WSPOLNE_H

#include <signal.h>
#include <sys/sem.h>
#include <errno.h>
#include "pamiec_wspoldzielona.h"
#include "semafory.h"
#include "logi.h"

//Klucz dla wspólnej kolejki komunikatów (jedna kolejka dla wszystkich)
#define KLUCZ_KOLEJKI 12345

//Globalna flaga sygnalu wyjscia (SIGQUIT w potomkach) - dostepna dla wszystkich procesow
extern volatile sig_atomic_t g_sygnal_wyjscia;

//Makro sprawdzajace czy proces powinien sie zakonczyc
#define CZY_KONCZYC(stan) ((stan)->flaga_ewakuacji || g_sygnal_wyjscia)

//Wspólny handler SIGQUIT dla procesów pochodnych (ustawia g_sygnal_wyjscia)
void ObslugaSygnaluWyjscia(int sig);

//Inicjalizacja procesu pochodnego (pamiec, semafory, logowanie)
int InicjalizujProcesPochodny(StanSklepu** stan, int* sem_id, const char* nazwa_procesu);

//Usuwa element z kolejki tablicowej i przesuwa pozostale elementy
int UsunZKolejki(int* kolejka, int* liczba, int wartosc_do_usuniecia);

#define MSG_TYPE_KASA_1 1
#define MSG_TYPE_KASA_2 2
#define MSG_TYPE_SAMOOBSLUGA 3
#define MSG_TYPE_PRACOWNIK 4

//Kody operacji dla pracownika
#define OP_WERYFIKACJA_WIEKU 1
#define OP_ODBLOKOWANIE_KASY 2

typedef struct {
    long mtype;       // Typ komunikatu: 1=Do Kasy 1, 2=Do Kasy 2, 3=Do Samoobslugi, 4=Do Pracownika, (100+ID)=Do Klienta
    int id_klienta;   // Dane: ID klienta
    int liczba_produktow;
    double suma_koszyka;
    int ma_alkohol;
    int wiek;
    int operacja;     // Typ zlecenia dla pracownika
} Komunikat;

//Blokujace czekanie na semafor z timeoutem
int CzekajNaSemafor(int sem_id, int sem_num, int sek_timeout);

//Blokujace czekanie na semafor az do przerwania przez sygnal (np. SIGCHLD)
int CzekajNaSygnal(int sem_id);

#endif
