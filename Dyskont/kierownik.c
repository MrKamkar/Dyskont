#include "kierownik.h"
#include "semafory.h"
#include "logi.h"
#include <string.h>
#include <unistd.h>

//Wyswietla menu kierownika
void WyswietlMenu() {
    printf("PANEL KIEROWNIKA SKLEPU:\n");
    printf("    1. Otworz kase stacjonarna 2\n");
    printf("    2. Zamknij kase stacjonarna 1\n");
    printf("    3. Zamknij kase stacjonarna 2\n");
    printf("    4. EWAKUACJA (zamknij sklep)\n");
    printf("    5. Pokaz status kas\n");
    printf("    0. Wyjscie\n");
    printf("Wybor: ");
}

//Otwiera kase stacjonarna 2
void OtworzKase2(StanSklepu* stan, int sem_id) {
    if (!stan) return;
    
    ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    
    if (stan->kasy_stacjonarne[1].stan == KASA_ZAMKNIETA) {
        stan->kasy_stacjonarne[1].stan = KASA_WOLNA;
        stan->kasy_stacjonarne[1].czas_ostatniej_obslugi = time(NULL);
        ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
        
        //Sygnal dla kasjera - kasa otwarta
        ZwolnijSemafor(sem_id, SEM_OTWORZ_KASA_STACJ_2);
        
        ZapiszLog(LOG_INFO, "Kierownik: Otwarto kase stacjonarna 2.");
        printf("Kasa stacjonarna 2 zostala otwarta.\n");
    } else {
        ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
        printf("Kasa stacjonarna 2 jest juz otwarta lub zajeta.\n");
    }
}

//Zamyka wskazana kase stacjonarna (obsluguje klientow przed zamknieciem)
void ZamknijKase(int id_kasy, StanSklepu* stan, int sem_id) {
    if (!stan || id_kasy < 0 || id_kasy >= LICZBA_KAS_STACJONARNYCH) return;
    
    char buf[256];
    
    ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    StanKasy aktualny_stan = stan->kasy_stacjonarne[id_kasy].stan;
    int w_kolejce = stan->kasy_stacjonarne[id_kasy].liczba_w_kolejce;
    ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    
    if (aktualny_stan == KASA_ZAMKNIETA) {
        printf("Kasa stacjonarna %d jest juz zamknieta.\n", id_kasy + 1);
        return;
    }
    
    if (w_kolejce > 0) {
        sprintf(buf, "Kierownik: Polecenie zamkniecia kasy %d (czeka: %d klientow - zostana obsluzeni).",
                id_kasy + 1, w_kolejce);
        ZapiszLog(LOG_INFO, buf);
        printf("Kasa %d zostanie zamknieta po obsluzeniu %d klientow.\n", id_kasy + 1, w_kolejce);
        
        //Ustawiamy polecenie, kasjer sam zamknie kase po obsluzeniu
        ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
        stan->polecenie_kierownika = POLECENIE_ZAMKNIJ_KASE;
        stan->id_kasy_do_zamkniecia = id_kasy;
        ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    } else {
        //Mozna zamknac od razu
        ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
        stan->kasy_stacjonarne[id_kasy].stan = KASA_ZAMKNIETA;
        ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
        
        sprintf(buf, "Kierownik: Zamknieto kase stacjonarna %d.", id_kasy + 1);
        ZapiszLog(LOG_INFO, buf);
        printf("Kasa stacjonarna %d zostala zamknieta.\n", id_kasy + 1);
    }
}

//Wydaje polecenie ewakuacji
void WydajPolecenie(int polecenie, int id_kasy, StanSklepu* stan, int sem_id) {
    if (!stan) return;
    
    ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    stan->polecenie_kierownika = polecenie;
    stan->id_kasy_do_zamkniecia = id_kasy;
    
    if (polecenie == POLECENIE_EWAKUACJA) {
        stan->flaga_ewakuacji = 1;
    }
    ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
}

//Wyswietla status kas
void PokazStatusKas(StanSklepu* stan, int sem_id) {
    if (!stan) return;
    
    ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    
    printf("\n--- STATUS KAS ---\n");
    printf("Klientow w sklepie: %d\n\n", stan->liczba_klientow_w_sklepie);
    
    printf("KASY STACJONARNE:\n");
    for (int i = 0; i < LICZBA_KAS_STACJONARNYCH; i++) {
        const char* status;
        switch (stan->kasy_stacjonarne[i].stan) {
            case KASA_ZAMKNIETA: status = "ZAMKNIETA"; break;
            case KASA_WOLNA: status = "WOLNA"; break;
            case KASA_ZAJETA: status = "ZAJETA"; break;
            case KASA_ZABLOKOWANA: status = "ZABLOKOWANA"; break;
            case KASA_ZAMYKANA: status = "ZAMYKANA"; break;
            default: status = "NIEZNANY";
        }
        printf("  Kasa %d: %s (w kolejce: %d)\n", 
               i + 1, status, stan->kasy_stacjonarne[i].liczba_w_kolejce);
    }
    
    printf("\nKASY SAMOOBSLUGOWE:\n");
    int czynne = 0;
    for (int i = 0; i < LICZBA_KAS_SAMOOBSLUGOWYCH; i++) {
        if (stan->kasy_samo[i].stan != KASA_ZAMKNIETA) czynne++;
    }
    printf("  Czynnych: %d/%d\n", czynne, LICZBA_KAS_SAMOOBSLUGOWYCH);
    printf("  W kolejce: %d\n", stan->liczba_w_kolejce_samo);
    
    ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    printf("------------------\n");
}

//Glowna funkcja procesu kierownika
#ifdef KIEROWNIK_STANDALONE
int main() {
    
    //Dolaczenie do pamieci wspoldzielonej
    StanSklepu* stan_sklepu = DolaczPamiecWspoldzielona();
    if (!stan_sklepu) {
        fprintf(stderr, "Kierownik: Nie mozna dolaczyc do pamieci wspoldzielonej\n");
        fprintf(stderr, "Upewnij sie, ze symulacja (dyskont.out) jest uruchomiona.\n");
        return 1;
    }
    
    //Dolaczenie do semaforow
    int sem_id = DolaczSemafory();
    if (sem_id == -1) {
        fprintf(stderr, "Kierownik: Nie mozna dolaczyc do semaforow\n");
        OdlaczPamiecWspoldzielona(stan_sklepu);
        return 1;
    }
    
    //Inicjalizacja systemu logowania (uzywa globalnej sciezki IPC_SCIEZKA)
    InicjalizujSystemLogowania();
    
    //Pobierz PID glownego procesu z pamieci wspoldzielonej
    pid_t pid_glowny = stan_sklepu->pid_glowny;
    if (pid_glowny <= 0) {
        fprintf(stderr, "Kierownik: Blad - nie znaleziono PID glownego procesu\n");
        OdlaczPamiecWspoldzielona(stan_sklepu);
        return 1;
    }
    
    ZapiszLog(LOG_INFO, "Kierownik: Proces uruchomiony.");
    printf("Panel kierownika uruchomiony. Polaczono z symulacja.\n");
    printf("PID procesu glownego: %d\n\n", pid_glowny);
    
    int wybor;
    int dzialaj = 1;
    
    while (dzialaj) {
        //Sprawdz czy symulacja nadal dziala
        if (stan_sklepu->flaga_ewakuacji) {
            printf("Symulacja zostala zakonczona (ewakuacja). Zamykam panel.\n");
            break;
        }
        
        WyswietlMenu();
        
        if (scanf("%d", &wybor) != 1) {
            //Czyszczenie bufora
            while (getchar() != '\n');
            printf("Nieprawidlowy wybor. Sprobuj ponownie.\n");
            continue;
        }
        
        switch (wybor) {
            case 0:
                dzialaj = 0;
                printf("Zamykam panel kierownika.\n");
                ZapiszLog(LOG_INFO, "Kierownik: Zakonczono prace.");
                break;
                
            case 1:
                //Wyslij sygnal SIGUSR1, otwieranie kasy 2
                if (kill(pid_glowny, SIGUSR1) == 0) {
                    printf("Wyslano sygnal SIGUSR1 - otwieranie kasy 2.\n");
                } else {
                    perror("Blad wysylania sygnalu SIGUSR1");
                }
                break;
                
            case 2:
                //Wyslij sygnal SIGUSR2, zamykanie kasy 1
                if (kill(pid_glowny, SIGUSR2) == 0) {
                    printf("Wyslano sygnal SIGUSR2 - zamykanie kasy 1.\n");
                } else {
                    perror("Blad wysylania sygnalu SIGUSR2");
                }
                break;
                
            case 3:
                //Zamykanie kasy 2 przez pamiec wspoldzielona, polecenie
                ZamknijKase(1, stan_sklepu, sem_id);
                break;
                
            case 4:
                printf("UWAGA: Czy na pewno chcesz oglosic ewakuacje? (1=tak, 0=nie): ");
                int potwierdz;
                if (scanf("%d", &potwierdz) == 1 && potwierdz == 1) {
                    //Wyslij sygnal SIGTERM, ewakuacja
                    if (kill(pid_glowny, SIGTERM) == 0) {
                        printf("Wyslano sygnal SIGTERM - EWAKUACJA!\n");
                        ZapiszLog(LOG_OSTRZEZENIE, "Kierownik: EWAKUACJA! Wyslano sygnal SIGTERM.");
                        dzialaj = 0;
                    } else {
                        perror("Blad wysylania sygnalu SIGTERM");
                    }
                } else {
                    printf("Anulowano.\n");
                }
                break;
                
            case 5:
                PokazStatusKas(stan_sklepu, sem_id);
                break;
                
            default:
                printf("Nieprawidlowy wybor. Sprobuj ponownie.\n");
        }
    }
    
    OdlaczPamiecWspoldzielona(stan_sklepu);
    return 0;
}
#endif
