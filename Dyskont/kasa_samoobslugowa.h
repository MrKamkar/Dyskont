#ifndef KASA_SAMOOBSLUGOWA_H
#define KASA_SAMOOBSLUGOWA_H

#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#include "pamiec_wspoldzielona.h"

//Czas skanowania jednego produktu (wolniejszy niz kasjer)
#define CZAS_SKANOWANIA_PRODUKTU_MS 300

//Szansa na losowa blokade kasy (1 na X produktow)
#define SZANSA_BLOKADY 2

//Czas oczekiwania na odblokowaanie kasy (w sekundach)
#define CZAS_OCZEKIWANIA_NA_ODBLOKOWANIE 5

//Maksymalny czas oczekiwania w kolejce (T) w sekundach
#define MAX_CZAS_KOLEJKI 30

//Funkcje zarzadzania kolejka samoobslugowa
int DodajDoKolejkiSamoobslugowej(int id_klienta, StanSklepu* stan, int sem_id);
int PobierzZKolejkiSamoobslugowej(StanSklepu* stan, int sem_id);
int ZnajdzWolnaKase(StanSklepu* stan, int sem_id);
int ZajmijKase(int id_kasy, int id_klienta, StanSklepu* stan, int sem_id);
void ZwolnijKase(int id_kasy, StanSklepu* stan, int sem_id);

//Funkcje dynamicznego zarzadzania kasami
int ObliczWymaganaLiczbeKas(int liczba_klientow);
void AktualizujLiczbeKas(StanSklepu* stan, int sem_id);

//Obsluga klienta
void ObsluzKlientaSamoobslugowo(int id_kasy, int id_klienta, int liczba_produktow, double suma, int ma_alkohol, int wiek, StanSklepu* stan, int sem_id);

#endif
