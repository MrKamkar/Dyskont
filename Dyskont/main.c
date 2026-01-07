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
#include "pracownik_obslugi.h"

//Globalne zmienne dla czyszczenia zasobow
static StanSklepu* g_stan_sklepu = NULL;
static int g_sem_id = -1;
static char g_sciezka[256];

//Handler sygnalu SIGINT - czyszczenie zasobow
void ObslugaSIGINT(int sig) {
    ZapiszLog(LOG_INFO, "Otrzymano SIGINT - rozpoczynam zamykanie systemu..");
    
    //Zamkniecie systemu logowania
    ZamknijSystemLogowania();
    
    //Odlaczenie i usuniecie pamieci wspoldzielonej
    if (g_stan_sklepu) {
        OdlaczPamiecWspoldzielona(g_stan_sklepu);
    }
    UsunPamiecWspoldzielona(g_sciezka);
    
    //Usuniecie semaforow
    if (g_sem_id != -1) {
        UsunSemafory(g_sem_id);
    }
    
    printf("System zamkniety.\n");
    exit(0);
}

int main(int argc, char* argv[]) {
    srand(time(NULL));
    
    //Parsowanie argumentu czasu symulacji
    int czas_symulacji_arg = 60;  //Domyslnie 60 sekund
    if (argc >= 2) {
        czas_symulacji_arg = atoi(argv[1]);
        if (czas_symulacji_arg <= 0) {
            fprintf(stderr, "Uzycie: %s [czas_symulacji_sekund]\n", argv[0]);
            fprintf(stderr, "Przyklad: %s 120  (symulacja 120 sekund)\n", argv[0]);
            return 1;
        }
    }
    
    //Zapis sciezki dla handlera sygnalu
    snprintf(g_sciezka, sizeof(g_sciezka), "%s", argv[0]);
    
    //Rejestracja handlera sygnalu
    signal(SIGINT, ObslugaSIGINT);
    
    //Inicjalizacja systemu logowania
    printf("=== Symulacja Dyskontu ===\n");
    printf("Czas symulacji: %d sekund\n", czas_symulacji_arg);
    InicjalizujSystemLogowania(argv[0]);
    UruchomProcesLogujacy();
    
    //Inicjalizacja pamieci wspoldzielonej
    ZapiszLog(LOG_INFO, "Inicjalizacja pamieci wspoldzielonej..");
    g_stan_sklepu = InicjalizujPamiecWspoldzielona(argv[0]);
    ZapiszLog(LOG_INFO, "Pamiec wspoldzielona zainicjalizowana pomyslnie.");
    
    //Inicjalizacja semaforow
    ZapiszLog(LOG_INFO, "Inicjalizacja semaforow..");
    g_sem_id = InicjalizujSemafory(argv[0]);
    if (g_sem_id == -1) {
        ZapiszLog(LOG_BLAD, "Nie udalo sie zainicjalizowac semaforow!");
        ObslugaSIGINT(0);
        return 1;
    }
    ZapiszLog(LOG_INFO, "Semafory zainicjalizowane pomyslnie.");
    
    //Uruchomienie procesow kasjerow (2 kasy stacjonarne)
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
            //Proces dziecka - uruchomienie kasjera przez exec
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
    
    //Inicjalizacja FIFO dla pracownika obslugi
    ZapiszLog(LOG_INFO, "Inicjalizacja FIFO obslugi..");
    if (InicjalizujFifoObslugi() == -1) {
        ZapiszLog(LOG_BLAD, "Nie udalo sie zainicjalizowac FIFO obslugi!");
    } else {
        ZapiszLog(LOG_INFO, "FIFO obslugi zainicjalizowane.");
    }
    
    //Uruchomienie procesu pracownika obslugi
    ZapiszLog(LOG_INFO, "Uruchamianie procesu pracownika obslugi..");
    pid_t pid_pracownik = fork();
    
    if (pid_pracownik == -1) {
        perror("Blad fork() dla pracownika obslugi");
        ZapiszLog(LOG_BLAD, "Nie udalo sie stworzyc procesu pracownika obslugi!");
    }
    else if (pid_pracownik == 0) {
        //Proces dziecka - uruchomienie pracownika przez exec
        execl("./pracownik", "pracownik", argv[0], (char*)NULL);
        
        perror("Blad exec() dla pracownika obslugi");
        exit(1);
    }
    else {
        char buf[256];
        sprintf(buf, "Uruchomiono proces pracownika obslugi [PID: %d]", pid_pracownik);
        ZapiszLog(LOG_INFO, buf);
    }
    
    //Uruchomienie procesow kas samoobslugowych (6 kas)
    ZapiszLog(LOG_INFO, "Uruchamianie procesow kas samoobslugowych..");
    pid_t pid_kas_samo[LICZBA_KAS_SAMO];
    
    for (int i = 0; i < LICZBA_KAS_SAMO; i++) {
        pid_t pid = fork();
        
        if (pid == -1) {
            perror("Blad fork() dla kasy samoobslugowej");
            ZapiszLog(LOG_BLAD, "Nie udalo sie stworzyc procesu kasy samoobslugowej!");
            continue;
        }
        else if (pid == 0) {
            //Proces dziecka - uruchomienie kasy samoobslugowej przez exec
            char id_str[16];
            sprintf(id_str, "%d", i);
            
            execl("./kasa_samo", "kasa_samo", argv[0], id_str, (char*)NULL);
            
            perror("Blad exec() dla kasy samoobslugowej");
            exit(1);
        }
        else {
            pid_kas_samo[i] = pid;
            char buf[256];
            sprintf(buf, "Uruchomiono proces kasy samoobslugowej [PID: %d, Kasa: %d]", pid, i + 1);
            ZapiszLog(LOG_INFO, buf);
        }
    }
    
    //=== PARAMETRY SYMULACJI ===
    int CZAS_SYMULACJI_SEK = czas_symulacji_arg;  //Z argumentu lub domyslnie 60
    int MAX_KLIENTOW_ROWNOCZESNIE = 100;
    int PRZERWA_MIEDZY_KLIENTAMI_MS = 50;
    
    char buf[256];
    sprintf(buf, "Symulacja bedzie trwac %d sekund. Max klientow rownoczesnie: %d", 
            CZAS_SYMULACJI_SEK, MAX_KLIENTOW_ROWNOCZESNIE);
    ZapiszLog(LOG_INFO, buf);
    
    //Tablica PID procesow klienckich (cykliczna)
    pid_t* pid_klientow = malloc(sizeof(pid_t) * MAX_KLIENTOW_ROWNOCZESNIE);
    int aktywnych_klientow = 0;
    int nastepny_slot = 0;
    int id_klienta = 0;
    int calkowita_liczba_klientow = 0;
    
    time_t czas_startu = time(NULL);
    time_t czas_konca = czas_startu + CZAS_SYMULACJI_SEK;
    
    ZapiszLog(LOG_INFO, "Rozpoczynam ciagla symulacje klientow...");
    
    //Glowna petla symulacji - tworzenie klientow przez okreslony czas
    while (time(NULL) < czas_konca && !g_stan_sklepu->flaga_ewakuacji) {
        
        //Zbierz zakonczone procesy (nieblokujaco)
        int status;
        pid_t zakonczone;
        while ((zakonczone = waitpid(-1, &status, WNOHANG)) > 0) {
            aktywnych_klientow--;
            if (WIFEXITED(status)) {
                int kod = WEXITSTATUS(status);
                if (kod == 0) {
                    sprintf(buf, "Klient [PID: %d] zakonczyl pomyslnie.", zakonczone);
                    ZapiszLog(LOG_INFO, buf);
                } else {
                    const char* opis;
                    switch (kod) {
                        case 1: opis = "niepelnoletni z alkoholem"; break;
                        case 2: opis = "kolejka pelna"; break;
                        default: opis = "nieznany blad"; break;
                    }
                    sprintf(buf, "Klient [PID: %d] zakonczyl: %s (kod %d)", zakonczone, opis, kod);
                    ZapiszLog(LOG_BLAD, buf);
                }
            }
        }
        
        //Tworz nowych klientow jesli jest miejsce
        while (aktywnych_klientow < MAX_KLIENTOW_ROWNOCZESNIE && 
               time(NULL) < czas_konca && !g_stan_sklepu->flaga_ewakuacji) {
            
            pid_t pid = fork();
            
            if (pid == -1) {
                perror("Blad fork()");
                usleep(10000); // 10ms przerwa przy bledzie
                break;
            }
            else if (pid == 0) {
                //Proces dziecka - uruchomienie klienta przez exec
                char id_str[16];
                sprintf(id_str, "%d", id_klienta + 1);
                
                execl("./klient", "klient", argv[0], id_str, (char*)NULL);
                
                perror("Blad exec()");
                exit(1);
            }
            else {
                //Proces rodzica
                pid_klientow[nastepny_slot] = pid;
                nastepny_slot = (nastepny_slot + 1) % MAX_KLIENTOW_ROWNOCZESNIE;
                aktywnych_klientow++;
                id_klienta++;
                calkowita_liczba_klientow++;
                
                //Krotka przerwa miedzy tworzeniem procesow
                usleep(PRZERWA_MIEDZY_KLIENTAMI_MS * 1000);
            }
        }
        
        //Wyswietlanie statusu co 5 sekund
        static time_t ostatni_status = 0;
        time_t teraz = time(NULL);
        if (teraz - ostatni_status >= 5) {
            sprintf(buf, "Symulacja: %ld/%d sek, klientow aktywnych: %d, razem: %d",
                    teraz - czas_startu, CZAS_SYMULACJI_SEK, aktywnych_klientow, calkowita_liczba_klientow);
            ZapiszLog(LOG_INFO, buf);
            ostatni_status = teraz;
        }
        
        usleep(10000); // 10ms glowna petla
    }
    
    sprintf(buf, "Koniec symulacji. Czekam na %d pozostalych klientow...", aktywnych_klientow);
    ZapiszLog(LOG_INFO, buf);
    
    //Oczekiwanie na zakonczenie pozostalych procesow klienckich
    while (aktywnych_klientow > 0) {
        int status;
        pid_t zakonczone = waitpid(-1, &status, 0);
        if (zakonczone > 0) {
            aktywnych_klientow--;
        }
    }
    
    free(pid_klientow);
    
    sprintf(buf, "Symulacja zakonczona. Laczna liczba klientow: %d", calkowita_liczba_klientow);
    ZapiszLog(LOG_INFO, buf);
    
    //Ustawienie flagi ewakuacji dla kasjerow
    ZapiszLog(LOG_INFO, "Zamykanie procesow kasjerow i kas samoobslugowych..");
    ZajmijSemafor(g_sem_id, SEM_PAMIEC_WSPOLDZIELONA);
    g_stan_sklepu->flaga_ewakuacji = 1;
    ZwolnijSemafor(g_sem_id, SEM_PAMIEC_WSPOLDZIELONA);
    
    //Oczekiwanie na zakonczenie procesow kasjerow
    for (int i = 0; i < LICZBA_KAS_STACJONARNYCH; i++) {
        int status;
        waitpid(pid_kasjerow[i], &status, 0);
        sprintf(buf, "Proces kasjera [Kasa: %d] zakonczony.", i + 1);
        ZapiszLog(LOG_INFO, buf);
    }
    
    //Oczekiwanie na zakonczenie procesow kas samoobslugowych
    for (int i = 0; i < LICZBA_KAS_SAMO; i++) {
        int status;
        waitpid(pid_kas_samo[i], &status, 0);
        sprintf(buf, "Proces kasy samoobslugowej [Kasa: %d] zakonczony.", i + 1);
        ZapiszLog(LOG_INFO, buf);
    }
    
    //Oczekiwanie na zakonczenie procesu pracownika obslugi
    int status;
    waitpid(pid_pracownik, &status, 0);
    ZapiszLog(LOG_INFO, "Proces pracownika obslugi zakonczony.");
    
    ZapiszLog(LOG_INFO, "Koniec symulacji.");
    
    //Czyszczenie zasobow
    ZapiszLog(LOG_INFO, "Zwalnianie zasobow IPC..");
    UsunFifoObslugi();
    OdlaczPamiecWspoldzielona(g_stan_sklepu);
    UsunPamiecWspoldzielona(argv[0]);
    UsunSemafory(g_sem_id);
    
    ZamknijSystemLogowania();
    
    printf("=== Symulacja zakonczona ===\n");
    return 0;
}
