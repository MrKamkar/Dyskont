#ifndef KIEROWNIK_H
#define KIEROWNIK_H

#include <sys/types.h>
#include <signal.h>
#include "pamiec_wspoldzielona.h"

//Funkcje kierownika
void WyswietlMenu();
void OtworzKase2(StanSklepu* stan, int sem_id);
void ZamknijKase(int id_kasy, StanSklepu* stan, int sem_id);
void WydajPolecenie(PolecenieKierownika polecenie, int id_kasy, StanSklepu* stan, int sem_id);

#endif