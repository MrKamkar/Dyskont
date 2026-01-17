#ifndef SEMAFORY_H
#define SEMAFORY_H

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "pamiec_wspoldzielona.h"

//Indeksy semaforów w tablicy - MUTEXY BINARNE (wzajemne wykluczanie)
#define MUTEX_PAMIEC_WSPOLDZIELONA 0  //Mutex chroniacy pamiec wspoldzielona
#define MUTEX_KOLEJKA_SAMO         1  //Mutex chroniacy kolejke kas samoobslugowych
#define MUTEX_KASA_STACJONARNA_1   2  //Mutex chroniacy kolejke kasy stacjonarnej 1
#define MUTEX_KASA_STACJONARNA_2   3  //Mutex chroniacy kolejke kasy stacjonarnej 2

//Indeksy semaforów w tablicy - SEMAFORY ZLICZAJACE (blokujace czekanie)
#define SEM_KLIENCI_KOLEJKA_1    4  //Liczba klientow w kolejce kasy 1
#define SEM_KLIENCI_KOLEJKA_2    5  //Liczba klientow w kolejce kasy 2

//Semafory sygnalizacyjne do eliminacji busy waiting
#define SEM_OTWORZ_KASA_STACJ_1    6  //Sygnal otwarcia kasy stacjonarnej 1
#define SEM_OTWORZ_KASA_STACJ_2    7  //Sygnal otwarcia kasy stacjonarnej 2
#define SEM_CZEKAJ_SYGNAL          8  //Semafor do blokujacego czekania

#define SEM_LICZBA               9  //Calkowita liczba semaforow

//Makra mapujace ID kasy na odpowiedni mutex/semafor
#define MUTEX_KASY(id) ((id) == 0 ? MUTEX_KASA_STACJONARNA_1 : MUTEX_KASA_STACJONARNA_2)
#define SEM_KLIENCI_KOLEJKA(id) ((id) == 0 ? SEM_KLIENCI_KOLEJKA_1 : SEM_KLIENCI_KOLEJKA_2)
#define SEM_OTWORZ_KASA_STACJ(id) ((id) == 0 ? SEM_OTWORZ_KASA_STACJ_1 : SEM_OTWORZ_KASA_STACJ_2)

//Struktura union wymagana przez semctl w niektórych systemach
union semun {
    int val;                //Wartość dla SETVAL
    struct semid_ds *buf;   //Bufor dla IPC_STAT, IPC_SET
    unsigned short *array;  //Tablica dla GETALL, SETALL
};

//Inicjalizuje semafory
int InicjalizujSemafory();

//Dolacza do istniejacych semaforow
int DolaczSemafory();

//Zajmuje semafor => zmniejsza wartosc o 1
int ZajmijSemafor(int sem_id, int sem_num);

//Zwalnia semafor => zwieksza wartosc o 1
int ZwolnijSemafor(int sem_id, int sem_num);

//Usuwa semafory z systemu.
int UsunSemafory(int sem_id);

//Ustawia wskaznik pamieci wspoldzielonej dla sprawdzania ewakuacji
void UstawPamiecDlaSemaforow(StanSklepu* stan);

#endif
