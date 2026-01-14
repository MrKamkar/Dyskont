#ifndef SEMAFORY_H
#define SEMAFORY_H

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

//Indeksy semaforów w tablicy (mutexy binarne)
#define SEM_PAMIEC_WSPOLDZIELONA 0  //Semafor pamieci wspoldzielonej
#define SEM_KOLEJKA_SAMO         1  //Semafor kolejki samoobslugowej
#define SEM_KASA_STACJONARNA_1   2  //Semafor dostepu do kolejki kasy 1
#define SEM_KASA_STACJONARNA_2   3  //Semafor dostepu do kolejki kasy 2

//Semafory zliczające (dla blokującego czekania)
#define SEM_WOLNE_KASY_SAMO      4  //Liczba wolnych kas samoobslugowych
#define SEM_KLIENCI_KOLEJKA_1    5  //Liczba klientow w kolejce kasy 1
#define SEM_KLIENCI_KOLEJKA_2    6  //Liczba klientow w kolejce kasy 2

#define SEM_LICZBA               7  //Calkowita liczba semaforow

//Makro mapujace ID kasy na semafor
#define SEM_KASY(id) ((id) == 0 ? SEM_KASA_STACJONARNA_1 : SEM_KASA_STACJONARNA_2)
#define SEM_KLIENCI_KOLEJKA(id) ((id) == 0 ? SEM_KLIENCI_KOLEJKA_1 : SEM_KLIENCI_KOLEJKA_2)

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

#endif
