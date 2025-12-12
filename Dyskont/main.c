#include <stdio.h>
#include "logi.h"

int main(int argc, char* argv[]) {
    InicjalizujSystemLogowania();
    UruchomProcesLogujacy();

    ZapiszLog(LOG_INFO, "Start symulacji");
    
    printf("Liczba argumentow: %d\n", argc);
    ZapiszLog(LOG_DEBUG, "Wypisano liczbe argumentow na ekran");

    for (int i = 0; i < argc; i++) {
        printf("Argument %d: %s\n", i, argv[i]);
    }
    ZapiszLog(LOG_DEBUG, "Wypisano argumenty");

    ZapiszLog(LOG_OSTRZEZENIE, "Przykladowe ostrzezenie");
    ZapiszLog(LOG_BLAD, "Przykladowy blad");

    ZapiszLog(LOG_INFO, "Koniec symulacji");
    ZamknijSystemLogowania();

    return 0;
}