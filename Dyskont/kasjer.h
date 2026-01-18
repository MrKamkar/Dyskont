#ifndef KASJER_H
#define KASJER_H

#include <unistd.h>
#include <time.h>
#include "pamiec_wspoldzielona.h"

//Czas obslugi jednego produktu (w milisekundach)
#define CZAS_OBSLUGI_PRODUKTU_MS 500

//Statusy kasjera
typedef enum {
    KASJER_NIEAKTYWNY,
    KASJER_CZEKA_NA_KLIENTA,
    KASJER_OBSLUGUJE,
    KASJER_ZAMYKA_KASE
} StanKasjera;

//Struktura kasjera (lokalna dla procesu)
typedef struct {
    int id_kasy;
    StanKasjera stan;
    time_t czas_ostatniej_aktywnosci;
} Kasjer;

//Funkcje kasjera
Kasjer* StworzKasjera(int id_kasy);
void UsunKasjera(Kasjer* kasjer);

void ObsluzKlienta(Kasjer* kasjer, int id_klienta, int liczba_produktow, double suma, StanSklepu* stan);

//Migracja klientow z kasy 1 do kasy 2 (przy otwarciu kasy 2)
int MigrujKlientowDoKasy2(StanSklepu* stan, int sem_id, int msg_id);

#endif
