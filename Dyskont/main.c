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

//Globalne zmienne dla obslugi SIGCHLD
static volatile sig_atomic_t g_aktywnych_klientow = 0;
static volatile sig_atomic_t g_calkowita_liczba_klientow = 0;

//Handler SIGCHLD - zbiera zakonczone procesy
void ObslugaSIGCHLD(int sig) {
    (void)sig;
    //Zbierz wszystkie zakonczone dzieci bez blokowania
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0) {
        g_aktywnych_klientow--;
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            g_calkowita_liczba_klientow++;
        }
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
    
    ZajmijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    g_stan_sklepu->flaga_ewakuacji = 1;
    g_stan_sklepu->polecenie_kierownika = 3;  //POLECENIE_EWAKUACJA
    ZwolnijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    
    ZapiszLog(LOG_OSTRZEZENIE, "Kierownik: Sygnal SIGTERM - EWAKUACJA! Klienci opuszczaja sklep.");
}

//Handler sygnalu SIGINT, czyszczenie zasobow
void ObslugaSIGINT(int sig) {
    (void)sig;
    ZapiszLog(LOG_INFO, "Otrzymano SIGINT - rozpoczynam zamykanie systemu..");
    
    //Zamkniecie systemu logowania
    ZamknijSystemLogowania();
    
    //Odlaczenie i usuniecie pamieci wspoldzielonej
    if (g_stan_sklepu) {
        OdlaczPamiecWspoldzielona(g_stan_sklepu);
    }
    UsunPamiecWspoldzielona();
    
    //Usuniecie semaforow
    if (g_sem_id != -1) {
        UsunSemafory(g_sem_id);
    }
    
    printf("System zamkniety.\n");
    exit(0);
}

int main(int argc, char* argv[]) {
    srand(time(NULL));
    
    //Pobranie czasu symulacji i opcjonalnych argumentow
    if (argc < 2) {
        fprintf(stderr, "Uzycie: %s <czas_symulacji_sekund> <nr_testu*> <max_klientow*>\n", argv[0]);
        fprintf(stderr, "  <czas_symulacji_sekund> - czas trwania symulacji\n");
        fprintf(stderr, "  <nr_testu*>             - tryb testu (0=normalny, 1=bez sleepow), domyslnie 0\n");
        fprintf(stderr, "  <max_klientow*>         - max klientow rownoczesnie, domyslnie 1000\n");
        return 1;
    }
    
    int czas_symulacji_arg = atoi(argv[1]);
    if (czas_symulacji_arg < 0) {
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
    signal(SIGINT, ObslugaSIGINT);
    signal(SIGCHLD, ObslugaSIGCHLD);  //Automatycznie gdy dziecko sie konczy
    signal(SIGUSR1, ObslugaSIGUSR1);  //Sygnal 1, otwieranie kasy 2
    signal(SIGUSR2, ObslugaSIGUSR2);  //Sygnal 2, zamykanie kasy
    signal(SIGTERM, ObslugaSIGTERM);  //Sygnal 3, ewakuacja
    
    //Inicjalizacja systemu logowania
    printf("=== Symulacja Dyskontu ===\n");
    printf("Czas symulacji: %d sekund\n", czas_symulacji_arg);
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
    
    //Zapisz PID glownego procesu, tryb testu i max klientow do pamieci wspoldzielonej
    g_stan_sklepu->pid_glowny = getpid();
    g_stan_sklepu->tryb_testu = tryb_testu;
    g_stan_sklepu->max_klientow_rownoczesnie = max_klientow;
    
    ZapiszLog(LOG_INFO, "Pamiec wspoldzielona zainicjalizowana pomyslnie.");
    
    //Inicjalizacja semaforow
    ZapiszLog(LOG_INFO, "Inicjalizacja semaforow..");
    g_sem_id = InicjalizujSemafory();
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
    
    //Czas symulacji (z argumentu)
    int CZAS_SYMULACJI_SEK = czas_symulacji_arg;
    
    char buf[256];
    sprintf(buf, "Symulacja bedzie trwac %d sekund. Max klientow rownoczesnie: %d", 
            CZAS_SYMULACJI_SEK, g_stan_sklepu->max_klientow_rownoczesnie);
    ZapiszLog(LOG_INFO, buf);
    
    //Zmienna dla tworzenia procesow
    int id_klienta = 0;
    
    time_t czas_startu = time(NULL);
    time_t czas_konca = czas_startu + CZAS_SYMULACJI_SEK;
    
    ZapiszLog(LOG_INFO, "Rozpoczynam ciagla symulacje klientow...");
    
    //Ustawienie alarmu co 1 sekunde dla statusu i kontroli czasu
    alarm(1);
    signal(SIGALRM, SIG_IGN);  //Ignorujemy SIGALRM, tylko przerywa pause()
    
    //Glowna petla symulacji - BEZ POLLINGU!
    while (time(NULL) < czas_konca && !g_stan_sklepu->flaga_ewakuacji) {
        
        //Tworz nowych klientow jesli jest miejsce
        while (g_aktywnych_klientow < g_stan_sklepu->max_klientow_rownoczesnie && 
               time(NULL) < czas_konca && !g_stan_sklepu->flaga_ewakuacji) {
            
            pid_t pid = fork();
            
            if (pid == -1) {
                perror("Blad fork()");
                break;
            }
            else if (pid == 0) {
                //Proces dziecka, uruchomienie klienta przez exec
                char id_str[16];
                sprintf(id_str, "%d", id_klienta + 1);
                
                execl("./klient", "klient", id_str, (char*)NULL);
                
                perror("Blad exec()");
                exit(1);
            }
            else {
                //Proces rodzica
                g_aktywnych_klientow++;
                id_klienta++;
                
                //Krotka przerwa miedzy tworzeniem procesow (pomijana w trybie testu)
                SYMULACJA_USLEEP(g_stan_sklepu, PRZERWA_MIEDZY_KLIENTAMI_MS * 1000);
            }
        }
        
        //Wyswietlanie statusu co 1 sekunde
        time_t teraz = time(NULL);
        sprintf(buf, "Symulacja: %ld/%d sek, klientow aktywnych: %d, utworzono: %d",
                teraz - czas_startu, CZAS_SYMULACJI_SEK, (int)g_aktywnych_klientow, id_klienta);
        ZapiszLog(LOG_INFO, buf);
        
        //Blokujace oczekiwanie na sygnal (SIGCHLD, SIGALRM, itp.) - BEZ POLLINGU!
        alarm(1);  //Nastepny alarm za 1 sek
        pause();   //Proces zasypia do sygnalu - 0% CPU!
    }
    
    alarm(0);  //Wylacz alarm
    
    sprintf(buf, "Koniec symulacji. Czekam na %d pozostalych klientow...", (int)g_aktywnych_klientow);
    ZapiszLog(LOG_INFO, buf);
    
    //Oczekiwanie na zakonczenie pozostalych procesow klienckich (blokujace)
    while (g_aktywnych_klientow > 0) {
        pause();  //Czeka na SIGCHLD
    }
    
    sprintf(buf, "Symulacja zakonczona. Laczna liczba klientow pomyslnie: %d", (int)g_calkowita_liczba_klientow);
    ZapiszLog(LOG_INFO, buf);
    
    //Ustawienie flagi ewakuacji dla kasjerow
    ZapiszLog(LOG_INFO, "Zamykanie procesow kasjerow i kas samoobslugowych..");
    ZajmijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    g_stan_sklepu->flaga_ewakuacji = 1;
    ZwolnijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    
    //Oczekiwanie na zakonczenie procesow kasjerow
    for (int i = 0; i < LICZBA_KAS_STACJONARNYCH; i++) {
        int status;
        waitpid(pid_kasjerow[i], &status, 0);
        sprintf(buf, "Proces kasjera [Kasa: %d] zakonczony.", i + 1);
        ZapiszLog(LOG_INFO, buf);
    }
    
    //Oczekiwanie na zakonczenie procesow kas samoobslugowych
    for (int i = 0; i < LICZBA_KAS_SAMOOBSLUGOWYCH; i++) {
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
    UsunPamiecWspoldzielona();
    UsunSemafory(g_sem_id);
    
    ZamknijSystemLogowania();
    
    printf("=== Symulacja zakonczona ===\n");
    return 0;
}
