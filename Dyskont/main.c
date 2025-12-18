#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "logi.h"
#include "klient.h"
#include "pamiec_wspoldzielona.h"

int main(int argc, char* argv[]) {
    srand(time(NULL));
    
    // Inicjalizacja systemu logowania (najpierw!)
    InicjalizujSystemLogowania(argv[0]);
    UruchomProcesLogujacy();
    
    // Inicjalizacja pamięci współdzielonej
    ZapiszLog(LOG_INFO, "Inicjalizacja pamieci wspoldzielonej...");
    StanSklepu* stan_sklepu = InicjalizujPamiecWspoldzielona(argv[0]);
    ZapiszLog(LOG_INFO, "Pamiec wspoldzielona zainicjalizowana pomyslnie.");

    ZapiszLog(LOG_INFO, "Start symulacji testowej klienta");

    // Test Klienta z logowaniem
    Klient* k1 = StworzKlienta(1);
    char buf[256];
    
    // Logowanie utworzenia
    sprintf(buf, "Klient [ID: %d] wszedl do sklepu. Wiek: %d lat. Planuje kupic %d produktow.", 
            k1->id, k1->wiek, k1->ilosc_planowana);
    ZapiszLog(LOG_INFO, buf);

    // Symulacja zakupów
    ZapiszLog(LOG_INFO, "Klient rozpoczyna zakupy...");
    ZrobZakupy(k1, stan_sklepu);
    ZapiszLog(LOG_INFO, "Klient zakonczyl zakupy.");

    // Raport zawartości koszyka
    sprintf(buf, "Klient ma %d produktow w koszyku. Lacznia kwota: %.2f PLN", 
            k1->liczba_produktow, ObliczSumeKoszyka(k1));
    ZapiszLog(LOG_INFO, buf);

    // Wypisanie szczegółów produktów (opcjonalne, dla debugu)
    for (int i = 0; i < k1->liczba_produktow; i++) {
        sprintf(buf, "- %s (%.2f PLN)", k1->koszyk[i].nazwa, k1->koszyk[i].cena);
        ZapiszLog(LOG_DEBUG, buf);
    }

    // Weryfikacja alkoholu
    if (CzyZawieraAlkohol(k1)) {
        ZapiszLog(LOG_OSTRZEZENIE, "W koszyku znajduje sie alkohol.");
        if (k1->wiek < 18) {
            ZapiszLog(LOG_BLAD, "ALARM: Klient niepelnoletni probuje kupic alkohol!");
        } else {
            ZapiszLog(LOG_INFO, "Weryfikacja wieku: Klient pelnoletni. Sprzedaz dozwolona.");
        }
    } else {
        ZapiszLog(LOG_INFO, "Brak alkoholu w koszyku.");
    }

    UsunKlienta(k1);
    ZapiszLog(LOG_INFO, "Klient opuscil sklep.");

    ZapiszLog(LOG_INFO, "Koniec symulacji");
    
    // Czyszczenie pamięci współdzielonej
    ZapiszLog(LOG_INFO, "Zwalnianie pamieci wspoldzielonej...");
    OdlaczPamiecWspoldzielona(stan_sklepu);
    UsunPamiecWspoldzielona(argv[0]);
    
    ZamknijSystemLogowania();

    return 0;
}