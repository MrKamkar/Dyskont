#ifndef KASJER_H
#define KASJER_H

#include <unistd.h>
#include <time.h>
#include "pamiec_wspoldzielona.h"

//Czas skasowania jednego produktu
#define CZAS_SKASOWANIA_PRODUKTU_MS 500

//Struktura kasjera
typedef struct {
    int id_kasy;
    time_t czas_ostatniej_aktywnosci;
} Kasjer;

//Funkcje do tworzenia i usuwania kasjera
Kasjer* StworzKasjera(int id_kasy);
void UsunKasjera(Kasjer* kasjer);

//Funkcje do obslugi klienta przez kasjera
void ObsluzKlienta(Kasjer* kasjer, int id_klienta, int liczba_produktow, double suma, StanSklepu* stan);

//Funkcja do przeniesienia klientow z kasy 1 do kasy 2
int PrzeniesKlientowDoKasy2(StanSklepu* stan, int sem_id, int msg_id);

#endif
