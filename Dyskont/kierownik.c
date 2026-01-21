#include "kierownik.h"
#include "wspolne.h"
#include "kolejki.h"
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>

#ifdef KIEROWNIK_STANDALONE

//Zmienne globalne
static StanSklepu* g_stan_sklepu = NULL;
static int g_sem_id = -1;

//Wyswietla menu kierownika
void WyswietlMenu() {
    printf("\n=== PANEL KIEROWNIKA ===\n");
    printf("  1. Otworz kase stacjonarna 2\n");
    printf("  2. Zamknij kase stacjonarna 1\n");
    printf("  3. Zamknij kase stacjonarna 2\n");
    printf("  4. EWAKUACJA (SIGTERM)\n");
    printf("  5. Pokaz status kolejek\n");
    printf("  0. Wyjscie\n");
    printf("Wybor: ");
    fflush(stdout);
}

//Wyswietla status kolejek (przez msgctl)
static void PokazStatusKolejek() {
    int msg_id_1 = PobierzIdKolejki(ID_IPC_KASA_1);
    int msg_id_2 = PobierzIdKolejki(ID_IPC_KASA_2);
    int msg_id_samo = PobierzIdKolejki(ID_IPC_SAMO);

    printf("\n--- STATUS KOLEJEK (msgctl) ---\n");
    printf("  Kasa stacjonarna 1: %d osob\n", PobierzRozmiarKolejki(msg_id_1));
    printf("  Kasa stacjonarna 2: %d osob\n", PobierzRozmiarKolejki(msg_id_2));
    printf("  Kasy samoobslugowe: %d osob\n", PobierzRozmiarKolejki(msg_id_samo));
    
    //Status kas z pamieci wspoldzielonej
    ZajmijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    int klienci = PobierzLiczbeKlientow(g_sem_id, g_stan_sklepu->max_klientow_rownoczesnie);
    printf("\n--- STATUS KAS ---\n");
    printf("  Klienci w sklepie: %d\n", klienci);
    printf("  Kasy samoobslugowe czynne: %u/%d\n", 
           g_stan_sklepu->liczba_czynnych_kas_samoobslugowych, LICZBA_KAS_SAMOOBSLUGOWYCH);
    
    for (int i = 0; i < LICZBA_KAS_STACJONARNYCH; i++) {
        const char* status;
        switch (g_stan_sklepu->kasy_stacjonarne[i].stan) {
            case KASA_ZAMKNIETA: status = "ZAMKNIETA"; break;
            case KASA_WOLNA: status = "WOLNA"; break;
            case KASA_ZAJETA: status = "ZAJETA"; break;
            case KASA_ZAMYKANA: status = "ZAMYKANA"; break;
            default: status = "?"; break;
        }
        printf("  Kasa stacjonarna %d: %s\n", i + 1, status);
    }
    ZwolnijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    
    printf("-------------------------------\n");
}

int main() {
    //Inicjalizacja procesu pochodnego
    if (InicjalizujProcesPochodny(&g_stan_sklepu, &g_sem_id, "Kierownik") == -1) {
        fprintf(stderr, "Blad: Upewnij sie, ze symulacja (dyskont.out) jest uruchomiona.\n");
        return 1;
    }
    
    pid_t pid_glowny = g_stan_sklepu->pid_glowny;
    if (pid_glowny <= 0) {
        fprintf(stderr, "Blad: Nie znaleziono PID glownego procesu.\n");
        OdlaczPamiecWspoldzielona(g_stan_sklepu);
        return 1;
    }
    
    printf("Panel kierownika uruchomiony.\n");
    printf("PID procesu glownego: %d\n", pid_glowny);
    
    int wybor;
    int dzialaj = 1;
    
    while (dzialaj) {
        WyswietlMenu();
        
        if (scanf("%d", &wybor) != 1) {
            while (getchar() != '\n');
            printf("Nieprawidlowy wybor.\n");
            continue;
        }
        
        switch (wybor) {
            case 0:
                dzialaj = 0;
                printf("Zamykam panel kierownika.\n");
                break;
                
            case 1:
                //SIGUSR1 - otwieranie kasy 2
                if (kill(pid_glowny, SIGUSR1) == 0) {
                    printf("Wyslano SIGUSR1 - otwieranie kasy 2.\n");
                } else {
                    perror("Blad wysylania SIGUSR1");
                }
                break;
                
            case 2:
                //SIGUSR2 - zamykanie kasy 1
                ZajmijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
                g_stan_sklepu->id_kasy_do_zamkniecia = 0;
                ZwolnijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
                
                if (kill(pid_glowny, SIGUSR2) == 0) {
                    printf("Wyslano SIGUSR2 - zamykanie kasy 1.\n");
                } else {
                    perror("Blad wysylania SIGUSR2");
                }
                break;
                
            case 3:
                //Zamykanie kasy 2
                ZajmijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
                g_stan_sklepu->id_kasy_do_zamkniecia = 1;
                g_stan_sklepu->polecenie_kierownika = POLECENIE_ZAMKNIJ_KASE;
                ZwolnijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
                printf("Wyslano polecenie zamkniecia kasy 2.\n");
                break;
                
            case 4:
                printf("Czy na pewno chcesz oglosic EWAKUACJE? (1=tak, 0=nie): ");
                int potwierdz;
                if (scanf("%d", &potwierdz) == 1 && potwierdz == 1) {
                    if (kill(pid_glowny, SIGTERM) == 0) {
                        printf("Wyslano SIGTERM - EWAKUACJA!\n");
                        dzialaj = 0;
                    } else {
                        perror("Blad wysylania SIGTERM");
                    }
                } else {
                    printf("Anulowano.\n");
                }
                break;
                
            case 5:
                PokazStatusKolejek();
                break;
                
            default:
                printf("Nieprawidlowy wybor.\n");
        }
    }
    
    OdlaczPamiecWspoldzielona(g_stan_sklepu);
    return 0;
}
#endif
