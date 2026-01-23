#ifndef KASA_SAMOOBSLUGOWA_H
#define KASA_SAMOOBSLUGOWA_H

#include <unistd.h>
#include <time.h>
#include "pamiec_wspoldzielona.h"

//Szansa na losowa blokade kasy, 1 na X produktow
#define SZANSA_BLOKADY 20

//Czas skanowania jednego produktu (samoobsluga)
#define CZAS_SKANOWANIA_PRODUKTU_MS 3000

//Funkcje zarzadzania kasami samoobslugowymi
int ZajmijKase(int id_kasy, int id_klienta, StanSklepu* stan, int sem_id);
void ZwolnijKase(int id_kasy, StanSklepu* stan, int sem_id);

//Funkcje dynamicznego zarzadzania kasami samoobslugowymi
unsigned int ObliczWymaganaLiczbeKas(unsigned int liczba_klientow);
void ZaktualizujKasySamoobslugowe(StanSklepu* stan, int sem_id, unsigned int liczba_klientow);

//Obsluga klienta przy kasie samoobslugowej
int ObsluzKlientaSamoobslugowo(int id_kasy, int id_klienta, unsigned int liczba_produktow, double suma, int ma_alkohol, unsigned int wiek, StanSklepu* stan, int sem_id);

#endif
