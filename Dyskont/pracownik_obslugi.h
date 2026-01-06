#ifndef PRACOWNIK_OBSLUGI_H
#define PRACOWNIK_OBSLUGI_H

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "pamiec_wspoldzielona.h"

//Sciezka do FIFO dla zadan odblokowania kas
#define FIFO_OBSLUGA "/tmp/dyskont_obsluga_fifo"

//Typy zadan dla pracownika obslugi
typedef enum {
    ZADANIE_ODBLOKUJ_KASE,
    ZADANIE_WERYFIKUJ_WIEK
} TypZadania;

//Struktura zadania wysylanego przez FIFO
typedef struct {
    TypZadania typ;
    int id_kasy;        //Numer kasy samoobslugowej (0-5)
    int id_klienta;     //ID klienta
    int wiek_klienta;   //Wiek do weryfikacji (dla alkoholu)
} ZadanieObslugi;

//Inicjalizacja FIFO (tworzenie lacza nazwanego)
int InicjalizujFifoObslugi();

//Usuniecie FIFO
void UsunFifoObslugi();

//Wysylanie zadania do pracownika (przez kase samoobslugowa)
int WyslijZadanieObslugi(ZadanieObslugi* zadanie);

//Odbieranie zadania przez pracownika
int OdbierzZadanieObslugi(ZadanieObslugi* zadanie);

#endif
