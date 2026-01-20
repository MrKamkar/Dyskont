#ifndef SEMAFORY_H
#define SEMAFORY_H

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "pamiec_wspoldzielona.h"

#define MUTEX_PAMIEC_WSPOLDZIELONA 0 //Mutex chroniacy dostep do pamieci wspoldzielonej

//Semafory sygnalizacyjne
#define SEM_OTWORZ_KASA_STACJONARNA_1 1 //Sygnal otwarcia kasy stacjonarnej 1
#define SEM_OTWORZ_KASA_STACJONARNA_2 2 //Sygnal otwarcia kasy stacjonarnej 2

//Semafory do otwierania kas samoobslugowych
#define SEM_KASA_SAMOOBSLUGOWA_0 3
#define SEM_KASA_SAMOOBSLUGOWA_1 4
#define SEM_KASA_SAMOOBSLUGOWA_2 5
#define SEM_KASA_SAMOOBSLUGOWA_3 6
#define SEM_KASA_SAMOOBSLUGOWA_4 7
#define SEM_KASA_SAMOOBSLUGOWA_5 8

#define SEM_WEJSCIE_DO_SKLEPU 9 //Sygnal wpuszczenia klienta do sklepu

#define MUTEX_KOLEJKI_VIP 10 //Mutex dla operacji VIP na kolejkach

#define SEM_LICZBA 11 //Calkowita liczba semaforow


//Makra mapujace ID kasy na odpowiedni semafor
#define SEM_OTWORZ_KASA_STACJONARNA(id) ((id) == 0 ? SEM_OTWORZ_KASA_STACJONARNA_1 : SEM_OTWORZ_KASA_STACJONARNA_2)

//Struktura union wymagana przez semctl
union semun {
    int val;                //Wartość dla SETVAL
    struct semid_ds *buf;   //Bufor dla IPC_STAT, IPC_SET
    unsigned short *array;  //Tablica dla GETALL, SETALL
};

//Inicjalizuje semafory
int InicjalizujSemafory(int max_klientow);

//Dolacza do istniejacych semaforow
int DolaczSemafory();

//Zajmuje semafor => zmniejsza wartosc o 1
int ZajmijSemafor(int sem_id, int sem_num);

//Zwalnia semafor => zwieksza wartosc o 1
int ZwolnijSemafor(int sem_id, int sem_num);

//Usuwa semafory z systemu.
int UsunSemafory(int sem_id);

#endif
