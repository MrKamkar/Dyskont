#include "wspolne.h"
#include <stdio.h>
#include <string.h>

//Globalna flaga sygnalu wyjscia - definicja
volatile sig_atomic_t g_sygnal_wyjscia = 0;

//Wspólny handler SIGTERM
void ObslugaSygnaluWyjscia(int sig) {
    (void)sig;
    g_sygnal_wyjscia = 1;
}

//Inicjalizacja procesu pochodnego
int InicjalizujProcesPochodny(StanSklepu** stan, int* sem_id, const char* nazwa_procesu) {
    //Dolaczenie do pamieci wspoldzielonej
    *stan = DolaczPamiecWspoldzielona();
    if (!*stan) {
        fprintf(stderr, "%s: Nie mozna dolaczyc do pamieci wspoldzielonej\n", nazwa_procesu);
        return -1;
    }
    
    //Ustawienie pamieci dla semaforow (sprawdzanie flagi ewakuacji)
    UstawPamiecDlaSemaforow(*stan);
    
    //Dolaczenie do semaforow
    *sem_id = DolaczSemafory();
    if (*sem_id == -1) {
        fprintf(stderr, "%s: Nie mozna dolaczyc do semaforow\n", nazwa_procesu);
        OdlaczPamiecWspoldzielona(*stan);
        *stan = NULL;
        return -1;
    }
    
    //Inicjalizacja systemu logowania
    InicjalizujSystemLogowania();
    
    return 0;
}



//Blokujace czekanie na semafor z timeoutem
int CzekajNaSemafor(int sem_id, int sem_num, int sek_timeout) {
    struct timespec timeout = {sek_timeout, 0};
    struct sembuf op = {sem_num, -1, 0};
    
    if (semtimedop(sem_id, &op, 1, &timeout) == 0) {
        return 0;
    }
    return -1;
}

//Blokujace czekanie na semafor (przerywane przerwaniami)
int CzekajNaSygnal(int sem_id) {
     struct sembuf op = {SEM_CZEKAJ_SYGNAL, -1, 0};
    
    //Czekaj w nieskonczonosc na semaforze.
    int wynik = semop(sem_id, &op, 1);
    
    //Jezeli przerwano przez sygnal - to oczekiwane zachowanie
    if (wynik == -1 && errno == EINTR) {
        return 0;
    }
    
    //Inne bledy
    return wynik;
}
