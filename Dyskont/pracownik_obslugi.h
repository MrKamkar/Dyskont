#ifndef PRACOWNIK_OBSLUGI_H
#define PRACOWNIK_OBSLUGI_H

#include "pamiec_wspoldzielona.h"

//Wysylanie zadania do pracownika obslugi przez kase samoobslugowa
int WyslijZadanieObslugi(int id_kasy, int typ_operacji, int wiek, int sem_id);

#endif
