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
#define MUTEX_KOLEJKI_VIP 1 //Mutex dla operacji VIP na kolejkach

//Semafory sygnalizacyjne
#define SEM_OTWORZ_KASA_STACJONARNA_1 2 //Sygnal otwarcia kasy stacjonarnej 1
#define SEM_OTWORZ_KASA_STACJONARNA_2 3 //Sygnal otwarcia kasy stacjonarnej 2

#define SEM_WEJSCIE_DO_SKLEPU 4 //Ilosc wolnych miejsc w sklepie
#define SEM_NOWY_KLIENT 5 //Sygnal ze nowy klient wszedl (dla watku kas samoobslugowych)

#define SEM_LICZBA 6 //Calkowita liczba semaforow


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

//Pobiera liczbe klientow w sklepie (Max - sem_val)
int PobierzLiczbeKlientow(int sem_id, int max_klientow);

//Usuwa semafory z systemu.
int UsunSemafory(int sem_id);

#endif
