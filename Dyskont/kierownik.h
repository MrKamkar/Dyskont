#ifndef KIEROWNIK_H
#define KIEROWNIK_H

#include <sys/types.h>
#include <signal.h>
#include "pamiec_wspoldzielona.h"

//Polecenia kierownika (przechowywane w pamieci wspoldzielonej)
#define POLECENIE_ZAMKNIJ_KASE  2
#define POLECENIE_EWAKUACJA     3

//Funkcje kierownika
void WyswietlMenu();
void OtworzKase2(StanSklepu* stan, int sem_id);
void ZamknijKase(int id_kasy, StanSklepu* stan, int sem_id);
void WydajPolecenie(int polecenie, int id_kasy, StanSklepu* stan, int sem_id);

#endif
