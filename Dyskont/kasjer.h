#ifndef KASJER_H
#define KASJER_H

#include <unistd.h>
#include <time.h>
#include "pamiec_wspoldzielona.h"

//Czas skasowania jednego produktu
#define CZAS_SKASOWANIA_PRODUKTU_MS 100000

//Timeout bezczynnosci - kasa zamyka sie po 30s bez klienta
#define TIMEOUT_BEZCZYNNOSCI_S 30

//Prog otwarcia kasy 1 - gdy w wspolnej kolejce wiecej niz 3 klientow
#define PROG_OTWARCIA_KASY_1 3

#endif
