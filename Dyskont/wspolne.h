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

//Struktura komunikatu dla kasy stacjonarnej
typedef struct {
    long mtype;       //Typ komunikatu
    int id_klienta;   //ID klienta
    unsigned int liczba_produktow;
    double suma_koszyka;
    int ma_alkohol;
    unsigned int wiek;
} MsgKasaStacj;

//Struktura komunikatu dla kasy samoobslugowej
typedef struct {
    long mtype;
    int id_klienta;
    unsigned int liczba_produktow;
    double suma_koszyka;
    int ma_alkohol;
    unsigned int wiek;
    time_t timestamp; //Znacznik czasu wyslania
} MsgKasaSamo;

//Struktura komunikatu dla pracownika obslugi
typedef struct {
    long mtype;
    int id_kasy;      //ID kasy zglaszajacej
    int operacja;     //Typ zlecenia/wynik
    unsigned int wiek;
} MsgPracownik;


key_t GenerujKluczIPC(char id_projektu);
int InicjalizujProcesPochodny(StanSklepu** stan, int* sem_id, const char* nazwa_procesu);

#endif
