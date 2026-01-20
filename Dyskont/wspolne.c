#include "wspolne.h"
#include <stdio.h>
#include <string.h>
#include <sys/msg.h>

//Implementacja funkcji generujacej klucz
key_t GenerujKluczIPC(char id_projektu) {
    key_t klucz = ftok(IPC_SCIEZKA, id_projektu);
    if (klucz == -1) {
        char buf[64];
        sprintf(buf, "Blad generowania klucza IPC (id: %c)", id_projektu);
        perror(buf);
    }
    return klucz;
}


//Inicjalizacja procesu pochodnego
int InicjalizujProcesPochodny(StanSklepu** stan, int* sem_id, const char* nazwa_procesu) {
    
    //Dolaczenie do pamieci wspoldzielonej
    *stan = DolaczPamiecWspoldzielona();
    if (!*stan) {
        fprintf(stderr, "%s: Nie mozna dolaczyc do pamieci wspoldzielonej\n", nazwa_procesu);
        return -1;
    }
    
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