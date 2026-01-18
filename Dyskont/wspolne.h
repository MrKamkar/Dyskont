#ifndef WSPOLNE_H
#define WSPOLNE_H

#include <signal.h>
#include <sys/sem.h>
#include <errno.h>
#include "pamiec_wspoldzielona.h"
#include "semafory.h"
#include "logi.h"

//Identyfikatory projektow dla ftok()
#define ID_IPC_LOGI 'A'
#define ID_IPC_SEMAFORY 'M'
#define ID_IPC_PAMIEC 'S'
#define ID_IPC_KOLEJKA 'Q'

//Globalna flaga sygnalu wyjscia dla procesow potomnych
extern volatile sig_atomic_t g_sygnal_wyjscia;

//Makro sprawdzajace czy proces powinien sie zakonczyc
#define CZY_KONCZYC(stan) ((stan)->flaga_ewakuacji || g_sygnal_wyjscia)

//Wspólny handler SIGQUIT dla procesów pochodnych
void ObslugaSygnaluWyjscia(int sig);

//Inicjalizacja procesu pochodnego
int InicjalizujProcesPochodny(StanSklepu** stan, int* sem_id, const char* nazwa_procesu);

//Generuje klucz IPC dla podanego identyfikatora projektu
key_t GenerujKluczIPC(char id_projektu);

//Typy komunikatow
#define MSG_TYPE_KASA_1 1
#define MSG_TYPE_KASA_2 2
#define MSG_TYPE_SAMOOBSLUGA 3
#define MSG_TYPE_PRACOWNIK 4

//Kanal odpowiedzi
#define MSG_RES_SAMOOBSLUGA_BASE 10000
#define MSG_RES_STACJONARNA_BASE 20000
#define MSG_RES_PRACOWNIK_BASE   30000

//Kody operacji dla pracownika
#define OP_WERYFIKACJA_WIEKU 1
#define OP_ODBLOKOWANIE_KASY 2

//Struktura komunikatu
typedef struct {
    long mtype;       //Typ komunikatu
    int id_klienta;   //ID klienta
    unsigned int liczba_produktow;
    double suma_koszyka;
    int ma_alkohol;
    unsigned int wiek;
    int operacja;     //Typ zlecenia dla pracownika
    time_t timestamp; //Znacznik czasu wyslania (dla wykrywania zombie)
} Komunikat;

#endif
