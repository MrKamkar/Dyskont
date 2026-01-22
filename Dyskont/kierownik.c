#include "kierownik.h"
#include "pamiec_wspoldzielona.h"
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
    int msg_id_wspolna = PobierzIdKolejki(ID_IPC_KASA_WSPOLNA);
    int msg_id_1 = PobierzIdKolejki(ID_IPC_KASA_1);
    int msg_id_2 = PobierzIdKolejki(ID_IPC_KASA_2);
    int msg_id_samo = PobierzIdKolejki(ID_IPC_SAMO);

    //Pobierz stan kas z pamieci wspoldzielonej
    ZajmijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    StanKasy stan_kasy_1 = g_stan_sklepu->kasy_stacjonarne[0].stan;
    StanKasy stan_kasy_2 = g_stan_sklepu->kasy_stacjonarne[1].stan;
    int klienci = PobierzLiczbeKlientow(g_sem_id, g_stan_sklepu->max_klientow_rownoczesnie);
    unsigned int kasy_samo_czynne = g_stan_sklepu->liczba_czynnych_kas_samoobslugowych;
    ZwolnijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);

    printf("\n--- STATUS KOLEJEK (msgctl)---\n");
    
    //Wspolna kolejka wyswietlana tylko gdy obie kasy zamkniete
    int obie_zamkniete = (stan_kasy_1 == KASA_ZAMKNIETA) && (stan_kasy_2 == KASA_ZAMKNIETA);
    if (obie_zamkniete) {
        printf("  Kolejka do kas stacjonarnych: %d osob\n", PobierzRozmiarKolejki(msg_id_wspolna));
    } else {
        printf("  Kasa stacjonarna 1: %d osob\n", PobierzRozmiarKolejki(msg_id_1));
        printf("  Kasa stacjonarna 2: %d osob\n", PobierzRozmiarKolejki(msg_id_2));
    }
    printf("  Kasy samoobslugowe: %d osob\n", PobierzRozmiarKolejki(msg_id_samo));
    
    //Status kas
    printf("\n--- STATUS KAS ---\n");
    printf("  Klienci w sklepie: %d\n", klienci);
    printf("  Kasy samoobslugowe czynne: %u/%d\n", kasy_samo_czynne, LICZBA_KAS_SAMOOBSLUGOWYCH);
    
    const char* nazwy_stanow[] = {"ZAMKNIETA", "WOLNA", "ZAJETA", "ZAMYKANA"};
    printf("  Kasa stacjonarna 1: %s\n", nazwy_stanow[stan_kasy_1]);
    printf("  Kasa stacjonarna 2: %s\n", nazwy_stanow[stan_kasy_2]);
    
    printf("-------------------------------\n");
}

//Handler SIGTERM (wyslany przez glowny proces)
static void ObslugaSIGTERM(int sig) {
    (void)sig;
    printf("\n\n[AUTO-EXIT] Wykryto zakonczenie symulacji.\n");
    if (g_stan_sklepu) {
        //Wyczysc PID kierownika przed wyjsciem
        ZajmijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
        g_stan_sklepu->pid_kierownika = 0;
        ZwolnijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
        
        OdlaczPamiecWspoldzielona(g_stan_sklepu);
    }
    exit(0);
}

int main() {
    //Inicjalizacja procesu pochodnego
    if (InicjalizujProcesPochodny(&g_stan_sklepu, &g_sem_id, "Kierownik") == -1) {
        fprintf(stderr, "Blad: Upewnij sie, ze symulacja (dyskont.out) jest uruchomiona.\n");
        return 1;
    }
    
    //Rejestracja PID kierownika w pamieci wspoldzielonej
    ZajmijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    g_stan_sklepu->pid_kierownika = getpid();
    pid_t pid_glowny = g_stan_sklepu->pid_glowny;
    ZwolnijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);

    if (pid_glowny <= 0) {
        fprintf(stderr, "Blad: Nie znaleziono PID glownego procesu.\n");
        OdlaczPamiecWspoldzielona(g_stan_sklepu);
        return 1;
    }
    
    //Obsluga SIGTERM
    struct sigaction sa;
    sa.sa_handler = ObslugaSIGTERM;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    
    printf("Panel kierownika uruchomiony (PID: %d)\n", getpid());
    printf("PID procesu glownego: %d\n", pid_glowny);
    
    int wybor;
    int dzialaj = 1;
    
    while (dzialaj) {
        WyswietlMenu();
        
        if (scanf("%d", &wybor) != 1) {
            if (errno == EINTR) continue; //Przerwane sygnalem
            while (getchar() != '\n');
            printf("Nieprawidlowy wybor.\n");
            continue;
        }
        
        switch (wybor) {
            case 0:
                dzialaj = 0;
                printf("Zamykam panel kierownika\n");
                break;
                
            case 1:
                //SIGUSR1 - otwieranie kasy 2
                if (kill(pid_glowny, SIGUSR1) == 0) {
                    printf("Wyslano SIGUSR1 - otwieranie kasy 2\n");
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
                    printf("Wyslano SIGUSR2 - zamykanie kasy 1\n");
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
                printf("Wyslano polecenie zamkniecia kasy 2\n");
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
    
    //Wyczysc PID przy normalnym wyjsciu
    ZajmijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    g_stan_sklepu->pid_kierownika = 0;
    ZwolnijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    
    OdlaczPamiecWspoldzielona(g_stan_sklepu);
    return 0;
}
#endif
