#ifndef KASJER_H
#define KASJER_H

#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#include "pamiec_wspoldzielona.h"

// Czas oczekiwania na klienta przed zamknieciem kasy (w sekundach)
#define CZAS_BEZCZYNNOSCI_DO_ZAMKNIECIA 30

// Czas obslugi jednego produktu (w milisekundach)
#define CZAS_OBSLUGI_PRODUKTU_MS 200

// Statusy kasjera
typedef enum {
    KASJER_NIEAKTYWNY,
    KASJER_CZEKA_NA_KLIENTA,
    KASJER_OBSLUGUJE,
    KASJER_ZAMYKA_KASE
} StanKasjera;

// Struktura kasjera (lokalna dla procesu)
typedef struct {
    int id_kasy;
    StanKasjera stan;
    time_t czas_ostatniej_aktywnosci;
} Kasjer;

// Funkcje kasjera
Kasjer* StworzKasjera(int id_kasy);
void UsunKasjera(Kasjer* kasjer);

int PobierzKlientaZKolejki(int id_kasy, StanSklepu* stan, int sem_id);
int DodajDoKolejkiStacjonarnej(int id_kasy, int id_klienta, StanSklepu* stan, int sem_id);
void ObsluzKlienta(Kasjer* kasjer, int id_klienta, int liczba_produktow, double suma);
int CzyOtworzycKase1(StanSklepu* stan);

#endif
