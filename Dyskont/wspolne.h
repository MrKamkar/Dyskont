#ifndef WSPOLNE_H
#define WSPOLNE_H

#include <signal.h>
#include <sys/sem.h>
#include <errno.h>
#include "pamiec_wspoldzielona.h"
#include "semafory.h"
#include "logi.h"

//Globalna flaga sygnalu wyjscia (SIGTERM) - dostepna dla wszystkich procesow
extern volatile sig_atomic_t g_sygnal_wyjscia;

//Makro sprawdzajace czy proces powinien sie zakonczyc
#define CZY_KONCZYC(stan) ((stan)->flaga_ewakuacji || g_sygnal_wyjscia)

//Wspólny handler SIGTERM dla procesów pochodnych
void ObslugaSygnaluWyjscia(int sig);

//Inicjalizacja procesu pochodnego (pamiec, semafory, logowanie)
//Zwraca 0 = sukces, -1 = blad
int InicjalizujProcesPochodny(StanSklepu** stan, int* sem_id, const char* nazwa_procesu);

//Usuwa element z kolejki tablicowej i przesuwa pozostale elementy
//Zwraca 1 jesli znaleziono i usunieto, 0 jesli nie znaleziono
int UsunZKolejki(int* kolejka, int* liczba, int wartosc_do_usuniecia);

//Blokujace czekanie na semafor z timeoutem
//Zwraca 0 = semafor zajety, -1 = timeout lub blad
int CzekajNaSemafor(int sem_id, int sem_num, int sek_timeout);

//Blokujace czekanie na semafor az do przerwania przez sygnal (np. SIGCHLD)
//Zamiast pause() + alarm() - czyste IPC
//Zwraca 0 gdy sygnal przerwał, -1 przy bledzie
int CzekajNaSygnal(int sem_id);

#endif
