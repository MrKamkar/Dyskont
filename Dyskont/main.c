#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include "logi.h"
#include "klient.h"
#include "pamiec_wspoldzielona.h"
#include "semafory.h"
#include "pracownik_obslugi.h"
#include "kasjer.h"
#include "wspolne.h"

//Globalne zmienne dla czyszczenia zasobow
static StanSklepu* g_stan_sklepu = NULL;
static int g_sem_id = -1;
static int g_msg_id = -1;           // ID wspólnej kolejki komunikatów
static char g_sciezka[256];

//Globalne zmienne dla obslugi SIGCHLD
static int g_aktywnych_klientow = 0; // Teraz zwykly int, modyfikowany tylko w main
static volatile sig_atomic_t g_calkowita_liczba_klientow = 0;
static int g_is_parent = 1; // Flaga oznaczajaca glowny proces (do czyszczenia)
static volatile sig_atomic_t g_zadanie_zamkniecia = 0; // Flaga prosba o zamkniecie (Ctrl+C)

//Globalne zmienne dla mechanizmu Self-Pipe (IPC)
static int g_pipe_fd[2]; // [0] - odczyt (main), [1] - zapis (handler)

//Handler SIGCHLD - zbiera zakonczone procesy i powiadamia main przez PIPE
void ObslugaSIGCHLD(int sig) {
    (void)sig;
    int status;
    int saved_errno = errno;
    
    //Zbierz wszystkie zakonczone dzieci bez blokowania
    while (waitpid(-1, &status, WNOHANG) > 0) {
        //Self-Pipe Trick
        char dummy = 'x';
        write(g_pipe_fd[1], &dummy, 1);
        
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            g_calkowita_liczba_klientow++;
        }
    }
    errno = saved_errno;
}

//Aktualizuj licznik klientow
void AktualizujLicznikKlientow() {
    char buf[128];
    ssize_t bytes_read;
    while ((bytes_read = read(g_pipe_fd[0], buf, sizeof(buf))) > 0) {
        g_aktywnych_klientow -= bytes_read;
    }
}

//Otwieranie kasy stacjonarnej 2
void ObslugaSIGUSR1(int sig) {
    (void)sig;
    if (!g_stan_sklepu) return;
    
    ZajmijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    if (g_stan_sklepu->kasy_stacjonarne[1].stan == KASA_ZAMKNIETA) {
        g_stan_sklepu->kasy_stacjonarne[1].stan = KASA_WOLNA;
        g_stan_sklepu->kasy_stacjonarne[1].czas_ostatniej_obslugi = time(NULL);
        ZwolnijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
        
        //Sygnal dla kasjera - kasa otwarta
        ZwolnijSemafor(g_sem_id, SEM_OTWORZ_KASA_STACJ_2);
        
        ZapiszLog(LOG_INFO, "Kierownik: Sygnal SIGUSR1 - otwarto kase stacjonarna 2.");
        
        //Migracja klientow z kasy 1 do kasy 2
        MigrujKlientowDoKasy2(g_stan_sklepu, g_sem_id);
    } else {
        ZwolnijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
        ZapiszLog(LOG_OSTRZEZENIE, "Kierownik: Kasa 2 jest juz otwarta.");
    }
}

//Zamykanie kasy stacjonarnej
void ObslugaSIGUSR2(int sig) {
    (void)sig;
    if (!g_stan_sklepu) return;
    
    ZajmijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    //Oznacz kase jako zamykana, nowi klienci nie dolacza
    if (g_stan_sklepu->kasy_stacjonarne[0].stan != KASA_ZAMKNIETA) {
        g_stan_sklepu->kasy_stacjonarne[0].stan = KASA_ZAMYKANA;
        g_stan_sklepu->polecenie_kierownika = 2;  //POLECENIE_ZAMKNIJ_KASE
        g_stan_sklepu->id_kasy_do_zamkniecia = 0;
        ZwolnijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
        ZapiszLog(LOG_INFO, "Kierownik: Sygnal SIGUSR2 - kasa 1 zamyka sie.");
    } else {
        ZwolnijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
        ZapiszLog(LOG_OSTRZEZENIE, "Kierownik: Kasa 1 jest juz zamknieta.");
    }
}

//Ewakuacja sklepu
void ObslugaSIGTERM(int sig) {
    (void)sig;
    if (!g_stan_sklepu) return;
    
    //Ustaw flagi bez blokowania mutexow (async-signal-safe)
    g_stan_sklepu->flaga_ewakuacji = 1;
    g_stan_sklepu->polecenie_kierownika = 3;  //POLECENIE_EWAKUACJA
    
    //Nie uzywamy ZapiszLog bo uzywa mutexow/streamow - niebezpieczne w handlerze
    //const char* msg = "Kierownik: Otrzymano sygnał ewakuacji.\n";
    //write(STDERR_FILENO, msg, 40);
}

//Handler sygnalu SIGINT, czyszczenie zasobow
//Handler sygnalu SIGINT, czyszczenie zasobow
void ObslugaSIGINT(int sig) {
    (void)sig;
    
    if (g_stan_sklepu) {
        g_stan_sklepu->flaga_ewakuacji = 1; //To przerwie petle CzekajNaSygnal w semafory.c
    }
    
    //Ustaw flage zamkniecia (async-signal-safe)
    g_zadanie_zamkniecia = 1;
    
    //Wyslij SIGTERM do calej grupy procesow (0) - to obudzi spiace dzieci (usleep)
    //To jest ryzykowne (main tez dostanie), ale konieczne dla natychmiastowej reakcji spiacych dzieci.
    //Main musi byc odporny na SIGTERM (handler ustawia tylko flagi).
    kill(0, SIGTERM);

    //KONIEC - main loop wyjdzie, cleanup wykona sie w main
}

int main(int argc, char* argv[]) {
    srand(time(NULL));

    //Inicjalizacja potoku (IPC) dla Self-Pipe Trick
    if (pipe(g_pipe_fd) == -1) {
        perror("Blad pipe");
        return 1;
    }
    //Ustawienie odczytu na non-blocking
    int flags = fcntl(g_pipe_fd[0], F_GETFL, 0);
    fcntl(g_pipe_fd[0], F_SETFL, flags | O_NONBLOCK);
    
    //Ustawienie FD_CLOEXEC - pipe nie bedzie dziedziczony przez exec()
    fcntl(g_pipe_fd[0], F_SETFD, FD_CLOEXEC);
    fcntl(g_pipe_fd[1], F_SETFD, FD_CLOEXEC);
    
    //Pobranie czasu symulacji i opcjonalnych argumentow
    if (argc < 2) {
        fprintf(stderr, "Uzycie: %s <czas_symulacji_sekund> <nr_testu*> <max_klientow*>\n", argv[0]);
        fprintf(stderr, "  <czas_symulacji_sekund> - czas trwania symulacji\n");
        fprintf(stderr, "  <nr_testu*>             - tryb testu (0=normalny, 1=bez sleepow), domyslnie 0\n");
        fprintf(stderr, "  <max_klientow*>         - max klientow rownoczesnie, domyslnie 1000\n");
        return 1;
    }
    
    int czas_symulacji = atoi(argv[1]);
    if (czas_symulacji < 0) {
        fprintf(stderr, "Blad: Czas symulacji musi byc liczba wieksza od 0\n");
        return 1;
    }
    
    //Opcjonalny tryb testu (domyslnie 0)
    int tryb_testu = 0;
    if (argc >= 3) {
        tryb_testu = atoi(argv[2]);
        if (tryb_testu < 0 || tryb_testu > 1) {
            fprintf(stderr, "Blad: Nieprawidlowy tryb testu (dozwolone: 0, 1)\n");
            return 1;
        }
    }
    
    //Opcjonalny limit klientow rownoczesnie (domyslnie 1000)
    int max_klientow = MAX_KLIENTOW_ROWNOCZESNIE_DOMYSLNIE;
    if (argc >= 4) {
        max_klientow = atoi(argv[3]);
        if (max_klientow <= 0) {
            fprintf(stderr, "Blad: Maksymalna liczba klientow musi byc wieksza od 0\n");
            return 1;
        }
    }
    
    //Zapis sciezki dla handlera sygnalu
    snprintf(g_sciezka, sizeof(g_sciezka), "%s", argv[0]);
    
    //Rejestracja handlerow sygnalow
    signal(SIGINT, ObslugaSIGINT);    //Ctrl+C: graceful shutdown
    signal(SIGCHLD, ObslugaSIGCHLD);  //Automatycznie gdy dziecko sie konczy
    signal(SIGUSR1, ObslugaSIGUSR1);  //Otwieranie kasy 2
    signal(SIGUSR2, ObslugaSIGUSR2);  //Zamykanie kasy 1
    signal(SIGTERM, ObslugaSIGTERM);  //Ewakuacja
    signal(SIGQUIT, ObslugaSIGTERM);  //Ctrl+\: Ewakuacja
    
    
    //Inicjalizacja systemu logowania
    printf("=== Symulacja Dyskontu ===\n");
    printf("Czas symulacji: %d sekund\n", czas_symulacji);
    printf("Max klientow rownoczesnie: %d\n", max_klientow);
    if (tryb_testu == 1) {
        printf("TRYB TESTU: Bez sleepow symulacyjnych\n");
    }
    printf("PID glownego procesu: %d (do wysylania sygnalow)\n", getpid());
    InicjalizujSystemLogowania(argv[0]);
    UruchomWatekLogujacy();
    
    //Inicjalizacja pamieci wspoldzielonej
    ZapiszLog(LOG_INFO, "Inicjalizacja pamieci wspoldzielonej..");
    g_stan_sklepu = InicjalizujPamiecWspoldzielona();
    
    //Ustawienie pamieci dla semaforow (sprawdzanie flagi ewakuacji)
    UstawPamiecDlaSemaforow(g_stan_sklepu);
    
    //Zapisz PID glownego procesu, tryb testu i max klientow do pamieci wspoldzielonej
    g_stan_sklepu->pid_glowny = getpid();
    g_stan_sklepu->tryb_testu = tryb_testu;
    g_stan_sklepu->max_klientow_rownoczesnie = max_klientow;
    
    ZapiszLog(LOG_INFO, "Pamiec wspoldzielona zainicjalizowana pomyslnie.");
    
    //Inicjalizacja wspólnej kolejki komunikatów
    g_msg_id = msgget(KLUCZ_KOLEJKI, IPC_CREAT | 0600);
    if (g_msg_id == -1) {
        perror("Blad inicjalizacji kolejki komunikatow");
        ZapiszLog(LOG_BLAD, "Nie udalo sie zainicjalizowac kolejki komunikatow!");
        ObslugaSIGINT(0); return 1;
    }

    ZapiszLog(LOG_INFO, "Kolejka komunikatow zainicjalizowana pomyslnie.");
    
    //Inicjalizacja semaforow
    ZapiszLog(LOG_INFO, "Inicjalizacja semaforow..");
    g_sem_id = InicjalizujSemafory();
    if (g_sem_id == -1) {
        perror("Blad inicjalizacji semaforow");
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
        if (pid == 0) {
            g_is_parent = 0; // Dziecko nie sprzata zasobow
            
            //Proces dziecka, uruchomienie kasjera przez exec
            char id_str[16];
            sprintf(id_str, "%d", i);
            
            execl("./kasjer", "kasjer", id_str, (char*)NULL);
            
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
    

    
    //Uruchomienie procesu pracownika obslugi
    ZapiszLog(LOG_INFO, "Uruchamianie procesu pracownika obslugi..");
    pid_t pid_pracownik = fork();
    
    if (pid_pracownik == -1) {
        perror("Blad fork() dla pracownika obslugi");
        ZapiszLog(LOG_BLAD, "Nie udalo sie stworzyc procesu pracownika obslugi!");
    }
    else if (pid_pracownik == 0) {
        g_is_parent = 0; // Dziecko nie sprzata zasobow
        
        //Proces dziecka, uruchomienie pracownika przez exec
        execl("./pracownik", "pracownik", (char*)NULL);
        
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
    pid_t pid_kas_samo[LICZBA_KAS_SAMOOBSLUGOWYCH];
    
    for (int i = 0; i < LICZBA_KAS_SAMOOBSLUGOWYCH; i++) {
        pid_t pid = fork();
        
        if (pid == -1) {
            perror("Blad fork() dla kasy samoobslugowej");
            ZapiszLog(LOG_BLAD, "Nie udalo sie stworzyc procesu kasy samoobslugowej!");
            continue;
        }
        else if (pid == 0) {
            g_is_parent = 0; // Dziecko nie sprzata zasobow
            
            //Proces dziecka, uruchomienie kasy samoobslugowej przez exec
            char id_str[16];
            sprintf(id_str, "%d", i);
            
            execl("./kasa_samo", "kasa_samo", id_str, (char*)NULL);
            
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
    
    char buf[256];
    sprintf(buf, "Symulacja bedzie trwac %d sekund. Max klientow rownoczesnie: %d", 
            czas_symulacji, g_stan_sklepu->max_klientow_rownoczesnie);
    ZapiszLog(LOG_INFO, buf);
    
    //Zmienna dla tworzenia procesow
    int id_klienta = 0;
    
    time_t czas_startu = time(NULL);
    time_t czas_konca = czas_startu + czas_symulacji;
    
    ZapiszLog(LOG_INFO, "Rozpoczynam ciagla symulacje klientow...");
    
    
    //Glowna petla symulacji
    while (time(NULL) < czas_konca && !g_stan_sklepu->flaga_ewakuacji && !g_zadanie_zamkniecia) {
        
        //Tworz nowych klientow jesli jest miejsce
        while (g_aktywnych_klientow < g_stan_sklepu->max_klientow_rownoczesnie && 
               time(NULL) < czas_konca && !g_stan_sklepu->flaga_ewakuacji && !g_zadanie_zamkniecia) {
            
            pid_t pid = fork();
            
            if (pid == -1) {
                perror("Blad fork()");
                break;
            }
            else if (pid == 0) {
                g_is_parent = 0; // Dziecko nie sprzata zasobow
                
                //Proces dziecka, uruchomienie klienta przez exec
                char id_str[16];
                sprintf(id_str, "%d", id_klienta + 1);
                
                //Drugi argument "0" to stworzony przez dyskont
                execl("./klient", "klient", id_str, "0", (char*)NULL);
                
                perror("Blad exec()");
                exit(1);
            }
            else {
                //Aktualizuj licznik przed zwiekszeniem (odczytaj z potoku)
                AktualizujLicznikKlientow();
                
                g_aktywnych_klientow++; // Zwykle ++, bo jestesmy w main (synchronizacja przez pipe)

                id_klienta++;
                
                //Krotka przerwa miedzy tworzeniem procesow (pomijana w trybie testu)
                SYMULACJA_USLEEP(g_stan_sklepu, PRZERWA_MIEDZY_KLIENTAMI_MS * 1000);
            }
        }
        
        //Regularna aktualizacja licznika z potoku
        AktualizujLicznikKlientow();
        
        //Wyswietlanie statusu co 1 sekunde
        time_t teraz = time(NULL);
        sprintf(buf, "Symulacja: %ld/%d sek, klientow aktywnych: %d, utworzono: %d",
                teraz - czas_startu, czas_symulacji, (int)g_aktywnych_klientow, id_klienta);
        ZapiszLog(LOG_INFO, buf);
        
        CzekajNaSygnal(g_sem_id);
    }
    
    //Sprawdz czy to bylo Ctrl+C czy koniec czasu
    if (g_zadanie_zamkniecia) {
        ZapiszLog(LOG_INFO, "Otrzymano SIGINT - rozpoczynam zamykanie systemu..");
    }
    
    //NAJPIERW ustaw flage ewakuacji - klienci sprawdza ja i wyjda
    ZajmijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    g_stan_sklepu->flaga_ewakuacji = 1;
    ZwolnijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    
    //Wyslij SIGTERM do wszystkich procesow potomnych w grupie
    signal(SIGTERM, SIG_IGN);  //Glowny proces ignoruje SIGTERM
    kill(0, SIGTERM);          //Wyslij do calej grupy procesow
    signal(SIGTERM, ObslugaSIGTERM);  //Przywroc handler
    
    sprintf(buf, "Koniec symulacji - ewakuacja. Czekam na %d klientow...", (int)g_aktywnych_klientow);
    ZapiszLog(LOG_INFO, buf);
    
    //Czekaj na klientow - maja teraz flage ewakuacji
    time_t czas_oczekiwania_start = time(NULL);
    int timeout_klientow = 30;
    
    while (g_aktywnych_klientow > 0 && (time(NULL) - czas_oczekiwania_start) < timeout_klientow) {
        AktualizujLicznikKlientow();
        CzekajNaSygnal(g_sem_id);
    }
    
    if (g_aktywnych_klientow > 0) {
        sprintf(buf, "Timeout! Pozostalo %d klientow, wymuszam ewakuacje.", (int)g_aktywnych_klientow);
        ZapiszLog(LOG_OSTRZEZENIE, buf);
        
        //Ponownie wyslij SIGTERM do wszystkich procesow potomnych
        signal(SIGTERM, SIG_IGN);
        kill(0, SIGTERM);
        signal(SIGTERM, ObslugaSIGTERM);
        
        ZapiszLog(LOG_INFO, "Czekam 5s na zakonczenie klientow po ewakuacji...");
        time_t start_ewak = time(NULL);
        while (g_aktywnych_klientow > 0 && (time(NULL) - start_ewak) < 5) {
            CzekajNaSygnal(g_sem_id);
        }
        
        if (g_aktywnych_klientow > 0) {
             ZapiszLog(LOG_BLAD, "Klienci nie chca wyjsc po dobroci. Kill -9!");
             kill(0, SIGKILL);
        }
    }
    
    sprintf(buf, "Symulacja zakonczona. Laczna liczba klientow pomyslnie: %d", (int)g_calkowita_liczba_klientow);
    ZapiszLog(LOG_INFO, buf);
    
    //Zamykanie procesow pomocniczych
    ZapiszLog(LOG_INFO, "Zamykanie procesow kasjerow i kas samoobslugowych..");
    
    //Wyslij SIGTERM do wszystkich procesow pomocniczych aby wybudzic z semaforow
    for (int i = 0; i < LICZBA_KAS_STACJONARNYCH; i++) {
        kill(pid_kasjerow[i], SIGTERM);
    }
    for (int i = 0; i < LICZBA_KAS_SAMOOBSLUGOWYCH; i++) {
        kill(pid_kas_samo[i], SIGTERM);
    }
    kill(pid_pracownik, SIGTERM);
    
    int status;
    
    //Oczekiwanie na zakonczenie procesow pomocniczych
    time_t start_cleanup = time(NULL);
    int cleanup_timeout = 15;
    int procesy_pozostale = LICZBA_KAS_STACJONARNYCH + LICZBA_KAS_SAMOOBSLUGOWYCH + 1;
    
    while (procesy_pozostale > 0 && (time(NULL) - start_cleanup) < cleanup_timeout) {
        //Sprawdz kasjerow
        for (int i = 0; i < LICZBA_KAS_STACJONARNYCH; i++) {
            if (pid_kasjerow[i] > 0) {
                pid_t wynik = waitpid(pid_kasjerow[i], &status, WNOHANG);
                if (wynik > 0 || (wynik == -1 && errno == ECHILD)) {
                    sprintf(buf, "Proces kasjera [Kasa: %d] zakonczony.", i + 1);
                    ZapiszLog(LOG_INFO, buf);
                    pid_kasjerow[i] = -1;
                    procesy_pozostale--;
                }
            }
        }
        
        //Sprawdz kasy samoobslugowe
        for (int i = 0; i < LICZBA_KAS_SAMOOBSLUGOWYCH; i++) {
            if (pid_kas_samo[i] > 0) {
                pid_t wynik = waitpid(pid_kas_samo[i], &status, WNOHANG);
                if (wynik > 0 || (wynik == -1 && errno == ECHILD)) {
                    sprintf(buf, "Proces kasy samoobslugowej [Kasa: %d] zakonczony.", i + 1);
                    ZapiszLog(LOG_INFO, buf);
                    pid_kas_samo[i] = -1;
                    procesy_pozostale--;
                }
            }
        }
        
        //Sprawdz pracownika
        if (pid_pracownik > 0) {
            pid_t wynik = waitpid(pid_pracownik, &status, WNOHANG);
            if (wynik > 0 || (wynik == -1 && errno == ECHILD)) {
                ZapiszLog(LOG_INFO, "Proces pracownika obslugi zakonczony.");
                pid_pracownik = -1;
                procesy_pozostale--;
            }
        }
        
        if (procesy_pozostale > 0) {
            CzekajNaSygnal(g_sem_id);
        }
    }
    
    if (procesy_pozostale > 0) {
        sprintf(buf, "Timeout cleanup! Pozostalo %d procesow, wysylam SIGKILL.", procesy_pozostale);
        ZapiszLog(LOG_OSTRZEZENIE, buf);
        
        //Ostateczne rozwiazanie - SIGKILL dla wszystkich opornych
        for (int i = 0; i < LICZBA_KAS_STACJONARNYCH; i++) if (pid_kasjerow[i] > 0) kill(pid_kasjerow[i], SIGKILL);
        for (int i = 0; i < LICZBA_KAS_SAMOOBSLUGOWYCH; i++) if (pid_kas_samo[i] > 0) kill(pid_kas_samo[i], SIGKILL);
        if (pid_pracownik > 0) kill(pid_pracownik, SIGKILL);
        
        //Jeszcze chwila na posprzatanie przez OS
        sleep(1);
    }
    
    ZapiszLog(LOG_INFO, "Koniec symulacji.");
    
    //Czyszczenie zasobow IPC (tylko proces rodzica)
    if (g_is_parent) {
        ZapiszLog(LOG_INFO, "Zwalnianie zasobow IPC..");
        

        OdlaczPamiecWspoldzielona(g_stan_sklepu);
        UsunPamiecWspoldzielona();
        UsunSemafory(g_sem_id);
        
        if (g_msg_id != -1) msgctl(g_msg_id, IPC_RMID, NULL);
        
        ZapiszLog(LOG_INFO, "Usunieto kolejke komunikatow.");
        
        //Zamknij potok IPC
        close(g_pipe_fd[0]);
        close(g_pipe_fd[1]);
        
        //Zamkniecie loggera NA SAM KONIEC - procesy moga logowac az do zakonczenia
        ZamknijSystemLogowania();
    }
    
    if (g_is_parent) {
        if (g_zadanie_zamkniecia) {
            printf("\n=== System zamkniety (Ctrl+C) ===\n");
        } else {
            printf("\n=== Symulacja zakonczona ===\n");
        }
    }
    return 0;
}
