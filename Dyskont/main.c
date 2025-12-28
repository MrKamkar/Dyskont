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
    ZapiszLog(LOG_INFO, "Otrzymano SIGINT - rozpoczynam zamykanie systemu..");
    
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
    printf("=== Symulacja Dyskontu ===\n");
    InicjalizujSystemLogowania(argv[0]);
    UruchomProcesLogujacy();
    
    // Inicjalizacja pamieci wspoldzielonej
    ZapiszLog(LOG_INFO, "Inicjalizacja pamieci wspoldzielonej..");
    g_stan_sklepu = InicjalizujPamiecWspoldzielona(argv[0]);
    ZapiszLog(LOG_INFO, "Pamiec wspoldzielona zainicjalizowana pomyslnie.");
    
    // Inicjalizacja semaforow
    ZapiszLog(LOG_INFO, "Inicjalizacja semaforow..");
    g_sem_id = InicjalizujSemafory(argv[0]);
    if (g_sem_id == -1) {
        ZapiszLog(LOG_BLAD, "Nie udalo sie zainicjalizowac semaforow!");
        ObslugaSIGINT(0);
        return 1;
    }
    ZapiszLog(LOG_INFO, "Semafory zainicjalizowane pomyslnie.");
    
    // Uruchomienie procesow kasjerow (2 kasy stacjonarne)
    ZapiszLog(LOG_INFO, "Uruchamianie procesow kasjerow..");
    pid_t pid_kasjerow[LICZBA_KAS_STACJONARNYCH];
    
    for (int i = 0; i < LICZBA_KAS_STACJONARNYCH; i++) {
        pid_t pid = fork();
        
        if (pid == -1) {
            perror("Blad fork() dla kasjera");
            ZapiszLog(LOG_BLAD, "Nie udalo sie stworzyc procesu kasjera!");
            continue;
        }
        else if (pid == 0) {
            // Proces dziecka - uruchomienie kasjera przez exec
            char id_str[16];
            sprintf(id_str, "%d", i);
            
            execl("./kasjer", "kasjer", argv[0], id_str, (char*)NULL);
            
            perror("Blad exec() dla kasjera");
            exit(1);
        }
        else {
            pid_kasjerow[i] = pid;
            char buf[256];
            sprintf(buf, "Uruchomiono proces kasjera [PID: %d, Kasa: %d]", pid, i + 1);
            ZapiszLog(LOG_INFO, buf);
        }
    }
    
    // Liczba klientow do utworzenia
    int liczba_klientow = 10;  // Zwiekszona liczba dla testu
    char buf[256];
    sprintf(buf, "Uruchamianie %d procesow klientow..", liczba_klientow);
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
    ZapiszLog(LOG_INFO, "Oczekiwanie na zakonczenie wszystkich klientow..");
    for (int i = 0; i < liczba_klientow; i++) {
        int status;
        waitpid(pid_klientow[i], &status, 0);
        
        if (WIFEXITED(status)) {
            int kod = WEXITSTATUS(status);
            sprintf(buf, "Proces klienta [PID: %d] zakonczyl sie z kodem: %d", 
                    pid_klientow[i], kod);
            ZapiszLog(kod == 0 ? LOG_INFO : LOG_BLAD, buf);
        }
    }
    
    free(pid_klientow);
    
    ZapiszLog(LOG_INFO, "Wszystkie procesy klientow zakonczone.");
    
    // Ustawienie flagi ewakuacji dla kasjerow
    ZapiszLog(LOG_INFO, "Zamykanie procesow kasjerow..");
    ZajmijSemafor(g_sem_id, SEM_PAMIEC_WSPOLDZIELONA);
    g_stan_sklepu->flaga_ewakuacji = 1;
    ZwolnijSemafor(g_sem_id, SEM_PAMIEC_WSPOLDZIELONA);
    
    // Oczekiwanie na zakonczenie procesow kasjerow
    for (int i = 0; i < LICZBA_KAS_STACJONARNYCH; i++) {
        int status;
        waitpid(pid_kasjerow[i], &status, 0);
        sprintf(buf, "Proces kasjera [Kasa: %d] zakonczony.", i + 1);
        ZapiszLog(LOG_INFO, buf);
    }
    
    ZapiszLog(LOG_INFO, "Koniec symulacji.");
    
    // Czyszczenie zasobow
    ZapiszLog(LOG_INFO, "Zwalnianie zasobow IPC..");
    OdlaczPamiecWspoldzielona(g_stan_sklepu);
    UsunPamiecWspoldzielona(argv[0]);
    UsunSemafory(g_sem_id);
    
    ZamknijSystemLogowania();
    
    printf("=== Symulacja zakonczona ===\n");
    return 0;
}
