#ifndef SEMAFORY_H
#define SEMAFORY_H

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

//Indeksy semaforów w tablicy
#define SEM_PAMIEC_WSPOLDZIELONA 0  //Semafor pamieci wspoldzielonej
#define SEM_KOLEJKA_SAMO         1  //Semafor kolejki samoobsługowej
#define SEM_KASA_STACJONARNA_1   2  //Semafor dostępu do kolejki kasy 1
#define SEM_KASA_STACJONARNA_2   3  //Semafor dostępu do kolejki kasy 2
#define SEM_LICZBA               4  //Liczba semaforów

//Struktura union wymagana przez semctl w niektórych systemach
union semun {
    int val;                //Wartość dla SETVAL
    struct semid_ds *buf;   //Bufor dla IPC_STAT, IPC_SET
    unsigned short *array;  //Tablica dla GETALL, SETALL
};

//Inicjalizuje semafory
int InicjalizujSemafory(const char* sciezka);

//Dolacza do istniejacych semaforow
int DolaczSemafory(const char* sciezka);

//Zajmuje semafor => zmniejsza wartosc o 1
int ZajmijSemafor(int sem_id, int sem_num);

//Zwalnia semafor => zwieksza wartosc o 1
int ZwolnijSemafor(int sem_id, int sem_num);

//Usuwa semafory z systemu.
int UsunSemafory(int sem_id);

#endif
