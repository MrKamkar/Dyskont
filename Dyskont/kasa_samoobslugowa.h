#ifndef KASA_SAMOOBSLUGOWA_H
#define KASA_SAMOOBSLUGOWA_H

#include <unistd.h>
#include <time.h>
#include "pamiec_wspoldzielona.h"

// Czas skanowania jednego produktu (wolniejszy niz kasjer)
#define CZAS_SKANOWANIA_PRODUKTU_MS 800

//Szansa na losowa blokade kasy - 1 na X produktow (5%)
#define SZANSA_BLOKADY 20

//Funkcje zarzadzania kasami samoobslugowymi
int ZajmijKase(int id_kasy, int id_klienta, StanSklepu* stan, int sem_id);
void ZwolnijKase(int id_kasy, StanSklepu* stan, int sem_id);

//Funkcje dynamicznego zarzadzania kasami
int ObliczWymaganaLiczbeKas(int liczba_klientow);

//Obsluga klienta (zwraca 0 gdy sukces, -1 gdy timeout blokady, -2 gdy niepelnoletni)
int ObsluzKlientaSamoobslugowo(int id_kasy, int id_klienta, int liczba_produktow, double suma, int ma_alkohol, int wiek, StanSklepu* stan, int sem_id);

#endif
