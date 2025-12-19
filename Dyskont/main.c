#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include "logi.h"
#include "klient.h"
#include "pamiec_wspoldzielona.h"
#include "semafory.h"

// Globalne zmienne dla czyszczenia zasobow
static StanSklepu* g_stan_sklepu = NULL;
static int g_sem_id = -1;
static char g_sciezka[256];

// Handler sygnalu SIGINT - czyszczenie zasobow
void ObslugaSIGINT(int sig) {
    ZapiszLog(LOG_INFO, "Otrzymano SIGINT - rozpoczynam zamykanie systemu...");
    
    // Zamkniecie systemu logowania
    ZamknijSystemLogowania();
    
    // Odlaczenie i usuniecie pamieci wspoldzielonej
    if (g_stan_sklepu) {
        OdlaczPamiecWspoldzielona(g_stan_sklepu);
    }
    UsunPamiecWspoldzielona(g_sciezka);
    
    // Usuniecie semaforow
    if (g_sem_id != -1) {
        UsunSemafory(g_sem_id);
    }
    
    printf("System zamkniety.\n");
    exit(0);
}

int main(int argc, char* argv[]) {
    srand(time(NULL));
    
    // Zapis sciezki dla handlera sygnalu
    snprintf(g_sciezka, sizeof(g_sciezka), "%s", argv[0]);
    
    // Rejestracja handlera sygnalu
    signal(SIGINT, ObslugaSIGINT);
    
    // Inicjalizacja systemu logowania
    printf("=== Dyskont Symulator - Manager Process ===\n");
    InicjalizujSystemLogowania(argv[0]);
    UruchomProcesLogujacy();
    
    // Inicjalizacja pamieci wspoldzielonej
    ZapiszLog(LOG_INFO, "Inicjalizacja pamieci wspoldzielonej...");
    g_stan_sklepu = InicjalizujPamiecWspoldzielona(argv[0]);
    ZapiszLog(LOG_INFO, "Pamiec wspoldzielona zainicjalizowana pomyslnie.");
    
    // Inicjalizacja semaforow
    ZapiszLog(LOG_INFO, "Inicjalizacja semaforow...");
    g_sem_id = InicjalizujSemafory(argv[0]);
    if (g_sem_id == -1) {
        ZapiszLog(LOG_BLAD, "Nie udalo sie zainicjalizowac semaforow!");
        ObslugaSIGINT(0);
        return 1;
    }
    ZapiszLog(LOG_INFO, "Semafory zainicjalizowane pomyslnie.");
    
    // Liczba klientow do utworzenia
    int liczba_klientow = 5;
    char buf[256];
    sprintf(buf, "Uruchamianie %d procesow klientow...", liczba_klientow);
    ZapiszLog(LOG_INFO, buf);
    
    // Tablica PID procesow klienckich
    pid_t* pid_klientow = malloc(sizeof(pid_t) * liczba_klientow);
    
    // Tworzenie procesow klientow
    for (int i = 0; i < liczba_klientow; i++) {
        pid_t pid = fork();
        
        if (pid == -1) {
            // Blad fork
            perror("Blad fork()");
            ZapiszLog(LOG_BLAD, "Nie udalo sie stworzyc procesu klienta!");
            continue;
        }
        else if (pid == 0) {
            // Proces dziecka - uruchomienie klienta przez exec
            char id_str[16];
            sprintf(id_str, "%d", i + 1);
            
            // Sprawdzenie czy plik istnieje (dla lepszego debugowania)
            if (access("./klient", F_OK | X_OK) == -1) {
                perror("Blad: Nie znaleziono pliku wykonywalnego './klient' lub brak praw wykonywania");
                exit(1);
            }

            // Wykonanie programu klienta
            execl("./klient", "klient", argv[0], id_str, (char*)NULL);
            
            // Jesli doszlismy tutaj, exec sie nie powiodl
            perror("Blad exec()");
            exit(1);
        }
        else {
            // Proces rodzica - zapisanie PID dziecka
            pid_klientow[i] = pid;
            sprintf(buf, "Uruchomiono proces klienta [PID: %d, ID: %d]", pid, i + 1);
            ZapiszLog(LOG_INFO, buf);
            
            // Krotka przerwa miedzy tworzeniem procesow
            usleep(100000); // 100ms
        }
    }
    
    // Oczekiwanie na zakonczenie wszystkich procesow klienckich
    ZapiszLog(LOG_INFO, "Oczekiwanie na zakonczenie wszystkich klientow...");
    for (int i = 0; i < liczba_klientow; i++) {
        int status;
        waitpid(pid_klientow[i], &status, 0);
        
        if (WIFEXITED(status)) {
            sprintf(buf, "Proces klienta [PID: %d] zakonczyl sie z kodem: %d", 
                    pid_klientow[i], WEXITSTATUS(status));
            ZapiszLog(LOG_INFO, buf);
        }
    }
    
    free(pid_klientow);
    
    ZapiszLog(LOG_INFO, "Wszystkie procesy klientow zakonczone.");
    ZapiszLog(LOG_INFO, "Koniec symulacji.");
    
    // Czyszczenie zasobow
    ZapiszLog(LOG_INFO, "Zwalnianie zasobow IPC...");
    OdlaczPamiecWspoldzielona(g_stan_sklepu);
    UsunPamiecWspoldzielona(argv[0]);
    UsunSemafory(g_sem_id);
    
    ZamknijSystemLogowania();
    
    printf("=== Symulacja zakonczona ===\n");
    return 0;
}
