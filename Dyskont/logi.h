#ifndef LOGI_H
#define LOGI_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <pthread.h>
#include <stdarg.h>

//Typy komunikatow logowania
typedef enum {
    LOG_INFO,
    LOG_OSTRZEZENIE,
    LOG_BLAD,
    LOG_DEBUG
} TypLogu;

//Kody ANSI dla kolorow
#define KOLOR_RESET "\033[0m"
#define KOLOR_CZERWONY "\033[31m"
#define KOLOR_ZIELONY "\033[32m"
#define KOLOR_ZOLTY "\033[33m"
#define KOLOR_NIEBIESKI "\033[34m"

//Typ komunikatu konczacego prace loggera
#define TYP_KONIEC 999

//Struktura komunikatu
struct KomunikatLog {
    long typ_komunikatu;      //Odpowiednik mtype
    TypLogu typ_logu;         //Typ komunikatu logowania
    char tresc[128];          //Tresc wiadomosci
};

//Funkcje zarzadzajace
void InicjalizujSystemLogowania();
void UruchomWatekLogujacy();
void ZamknijSystemLogowania();

//Funkcje logowania
void ZapiszLog(TypLogu typ_logu, const char* tresc);
void ZapiszLogF(TypLogu typ_logu, const char* format, ...);

#endif
