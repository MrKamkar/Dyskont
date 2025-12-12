// Zabezpieczenie przed wielokrotnym zdefiniowaniem logi.h
#ifndef LOGI_H
#define LOGI_H

// Typy komunikatów logowania
#define LOG_INFO 0
#define LOG_OSTRZEZENIE 1
#define LOG_BLAD 2
#define LOG_DEBUG 3

// Kody ANSI dla kolorow
#define KOLOR_RESET "\033[0m"
#define KOLOR_CZERWONY "\033[31m"
#define KOLOR_ZIELONY "\033[32m"
#define KOLOR_ZOLTY "\033[33m"
#define KOLOR_NIEBIESKI "\033[34m"

// Indentyfikator kolejki
#define ID_KOLEJKI 12345

// Typ komunikatu konczacego prace loggera (musi byc > 0)
#define TYP_KONIEC 999

// Struktura komunikatu wymagana przez msgsnd/msgrcv
struct KomunikatLog {
    long typ_komunikatu;      // Odpowiednik mtype
    int typ_logu;             // Typ komunikatu logowania
    char tresc[256];          // Tresc wiadomosci
};

// Funkcje zarzadzajace
void InicjalizujSystemLogowania();
void UruchomProcesLogujacy();
void ZamknijSystemLogowania();

// Funkcja dla klienta
void ZapiszLog(int typ_logu, const char* format);

#endif