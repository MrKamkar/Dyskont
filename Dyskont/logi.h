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

// Typy komunikatów logowania
typedef enum {
    LOG_INFO,
    LOG_OSTRZEZENIE,
    LOG_BLAD,
    LOG_DEBUG
} TypLogu;

// Kody ANSI dla kolorow
#define KOLOR_RESET "\033[0m"
#define KOLOR_CZERWONY "\033[31m"
#define KOLOR_ZIELONY "\033[32m"
#define KOLOR_ZOLTY "\033[33m"
#define KOLOR_NIEBIESKI "\033[34m"

// Typ komunikatu konczacego prace loggera (musi byc > 0)
#define TYP_KONIEC 999

// Struktura komunikatu wymagana przez msgsnd/msgrcv
struct KomunikatLog {
    long typ_komunikatu;      // Odpowiednik mtype
    TypLogu typ_logu;         // Typ komunikatu logowania
    char tresc[256];          // Tresc wiadomosci
};

// Funkcje zarzadzajace
void InicjalizujSystemLogowania(const char* sciezka);
void UruchomProcesLogujacy();
void ZamknijSystemLogowania();

// Funkcja dla klienta
void ZapiszLog(TypLogu typ_logu, const char* format);

#endif