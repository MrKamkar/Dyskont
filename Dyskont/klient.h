#ifndef KLIENT_H
#define KLIENT_H

#include <time.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include "pamiec_wspoldzielona.h"

//Struktura klienta
typedef struct {
    pid_t id;
    unsigned int wiek;
    
    //Koszyk (tabela o stałym rozmiarze)
    Produkt* koszyk;
    unsigned int liczba_produktow;  //Aktualnie w koszyku
    unsigned int ilosc_planowana;   //Ile zamierza kupić (rozmiar tablicy)

    time_t czas_dolaczenia_do_kolejki;
} Klient;

//Funkcje zarzadzajace istnieniem klienta
Klient* StworzKlienta(int id);
void UsunKlienta(Klient* k);

//Operacje na kliencie
void ZrobZakupy(Klient* k, const StanSklepu* stan_sklepu);
int CzyZawieraAlkohol(const Klient* k);
double ObliczSumeKoszyka(const Klient* k);

//Wydruk paragonu z lista produktow
void WydrukujParagon(const Klient* k, const char* typ_kasy, int id_kasy);

#endif
