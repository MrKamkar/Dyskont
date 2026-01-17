#ifndef PRACOWNIK_OBSLUGI_H
#define PRACOWNIK_OBSLUGI_H

#include "wspolne.h"

//Wysylanie zadania do pracownika (przez kase samoobslugowa) - IPC Message Queue
//Zwraca 1 (OK) lub 0 (Odmowa/Blad)
int WyslijZadanieObslugi(int id_kasy, int typ_operacji, int wiek);

#endif
