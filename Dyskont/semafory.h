#ifndef SEMAFORY_H
#define SEMAFORY_H

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>

//Indeksy semaforów w tablicy - MUTEXY BINARNE (wzajemne wykluczanie)
#define MUTEX_PAMIEC_WSPOLDZIELONA 0  //Mutex chroniacy pamiec wspoldzielona
#define MUTEX_KOLEJKA_SAMO         1  //Mutex chroniacy kolejke kas samoobslugowych
#define MUTEX_KASA_STACJONARNA_1   2  //Mutex chroniacy kolejke kasy stacjonarnej 1
#define MUTEX_KASA_STACJONARNA_2   3  //Mutex chroniacy kolejke kasy stacjonarnej 2

//Indeksy semaforów w tablicy - SEMAFORY ZLICZAJACE (blokujace czekanie)
#define SEM_WOLNE_KASY_SAMO      4  //Liczba wolnych kas samoobslugowych
#define SEM_KLIENCI_KOLEJKA_1    5  //Liczba klientow w kolejce kasy 1
#define SEM_KLIENCI_KOLEJKA_2    6  //Liczba klientow w kolejce kasy 2

//Semafory sygnalizacyjne do eliminacji busy waiting
#define SEM_ODBLOKUJ_KASA_SAMO_0   7   //Sygnal odblokowania kasy samoobslugowej 0
#define SEM_ODBLOKUJ_KASA_SAMO_1   8   //Sygnal odblokowania kasy samoobslugowej 1
#define SEM_ODBLOKUJ_KASA_SAMO_2   9   //Sygnal odblokowania kasy samoobslugowej 2
#define SEM_ODBLOKUJ_KASA_SAMO_3   10  //Sygnal odblokowania kasy samoobslugowej 3
#define SEM_ODBLOKUJ_KASA_SAMO_4   11  //Sygnal odblokowania kasy samoobslugowej 4
#define SEM_ODBLOKUJ_KASA_SAMO_5   12  //Sygnal odblokowania kasy samoobslugowej 5
#define SEM_OTWORZ_KASA_STACJ_1    13  //Sygnal otwarcia kasy stacjonarnej 1
#define SEM_OTWORZ_KASA_STACJ_2    14  //Sygnal otwarcia kasy stacjonarnej 2
#define SEM_CZEKAJ_SYGNAL          15  //Semafor do blokujacego czekania (zamiast pause+alarm)

#define SEM_LICZBA               16  //Calkowita liczba semaforow

//Makra mapujace ID kasy na odpowiedni mutex/semafor
#define MUTEX_KASY(id) ((id) == 0 ? MUTEX_KASA_STACJONARNA_1 : MUTEX_KASA_STACJONARNA_2)
#define SEM_KLIENCI_KOLEJKA(id) ((id) == 0 ? SEM_KLIENCI_KOLEJKA_1 : SEM_KLIENCI_KOLEJKA_2)
#define SEM_OTWORZ_KASA_STACJ(id) ((id) == 0 ? SEM_OTWORZ_KASA_STACJ_1 : SEM_OTWORZ_KASA_STACJ_2)

//Makro z walidacja - arytmetyka wymaga poprawnego id
#define SEM_ODBLOKUJ_KASA_SAMO(id) \
    (assert((id) >= 0 && (id) < 6), \
     SEM_ODBLOKUJ_KASA_SAMO_0 + (id))

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
