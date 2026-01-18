#ifndef KASA_SAMOOBSLUGOWA_H
#define KASA_SAMOOBSLUGOWA_H

#include <unistd.h>
#include <time.h>
#include "pamiec_wspoldzielona.h"

// Czas skanowania jednego produktu (wolniejszy niz kasjer)
#define CZAS_SKANOWANIA_PRODUKTU_MS 800

//Szansa na losowa blokade kasy - 1 na X produktow (5%)
#define SZANSA_BLOKADY 20

//Komendy sterujace IPC
#define CMD_CLOSE -5

//Funkcje zarzadzania kasami samoobslugowymi
int ZajmijKase(int id_kasy, int id_klienta, StanSklepu* stan, int sem_id);
void ZwolnijKase(int id_kasy, StanSklepu* stan, int sem_id);

//Funkcje dynamicznego zarzadzania kasami
unsigned int ObliczWymaganaLiczbeKas(unsigned int liczba_klientow);
void ZaktualizujKasySamoobslugowe(StanSklepu* stan, int sem_id, unsigned int liczba_klientow);

//Obsluga klienta przy kasie samoobslugowej
int ObsluzKlientaSamoobslugowo(int id_kasy, int id_klienta, unsigned int liczba_produktow, 
                                 double suma, int ma_alkohol, unsigned int wiek, StanSklepu* stan, int sem_id);

#endif