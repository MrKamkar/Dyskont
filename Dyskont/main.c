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
#include "kasa_samoobslugowa.h"

//Globalne zmienne systemu
static StanSklepu* g_stan_sklepu = NULL; //Stan sklepu
static int g_sem_id = -1; //ID tablicy semaforow
static int g_msg_id = -1; //ID wspólnej kolejki komunikatów

//Globalne zmienne stanu symulacji
static unsigned int g_aktywnych_klientow = 0; //Liczba klientow w tej chwili
static volatile sig_atomic_t g_calkowita_liczba_klientow = 0; //Liczba wszystkich klientow
static int g_is_parent = 1; //Flaga mowiaca czy jestesmy w glownym procesie
static volatile sig_atomic_t g_zadanie_zamkniecia = 0; //Prosba o zamkniecie (Ctrl+C)

//Globalne zmienne dla mechanizmu pipe
static int g_pipe_fd[2]; //[0] dla odczytu, [1] dla zapisu


//Aktualizuj licznik klientow
void AktualizujLicznikKlientow() {
    char buf[128];
    ssize_t bytes_read;

    //Nie blokujace czytanie z pipe
    while ((bytes_read = read(g_pipe_fd[0], buf, sizeof(buf))) > 0) {
        unsigned int do_odjecia = (unsigned int)bytes_read;

        if (do_odjecia > g_aktywnych_klientow) g_aktywnych_klientow = 0;
        else g_aktywnych_klientow -= do_odjecia;
    }
}

//Czeka na dane dostarczone przez pipe w uspieniu z timeoutem
int CzekajNaZdarzenieIPC(int timeout_sec) {
    fd_set readfds;
    struct timeval tv;
    struct timeval* ptv = NULL;
    
    FD_ZERO(&readfds);
    FD_SET(g_pipe_fd[0], &readfds);
    
    if (timeout_sec >= 0) {
        tv.tv_sec = timeout_sec;
        tv.tv_usec = 0;
        ptv = &tv;
    }

    int retval = select(g_pipe_fd[0] + 1, &readfds, NULL, NULL, ptv);
    
    if (retval > 0) {
        return 1; //Dostalismy dane
    } else if (retval == 0) {
        return 0; //Czas minol
    } else {
        return -1; //Blad
    }
}

//Handler SIGCHLD zbierajacy zakonczone procesy i powiadamiajacy glowny proces poprzez PIPE
void ObslugaSIGCHLD(int sig) {
    (void)sig;
    int status;
    int saved_errno = errno;
    
    //Zbierz wszystkie zakonczone dzieci bez blokowania
    while (waitpid(-1, &status, WNOHANG) > 0) {
        char bajt = 'x';
        write(g_pipe_fd[1], &bajt, 1);
        
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            g_calkowita_liczba_klientow++;
        }
    }
    errno = saved_errno;
}

//Sygnal do otwarcia kasy stacjonarnej 2
void ObslugaSIGUSR1(int sig) {
    (void)sig;

    if (!g_stan_sklepu) return;
    
    ZajmijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA, g_stan_sklepu);
    if (g_stan_sklepu->kasy_stacjonarne[1].stan == KASA_ZAMKNIETA) {

        g_stan_sklepu->kasy_stacjonarne[1].stan = KASA_WOLNA;
        ZwolnijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
        
        //Sygnal dla kasjera ze kasa jest juz otwarta
        ZwolnijSemafor(g_sem_id, SEM_OTWORZ_KASA_STACJONARNA_2);
        
        ZapiszLog(LOG_INFO, "Kierownik: Sygnal SIGUSR1 => otwarto kase stacjonarna 2.");
        
        //Migracja klientow z kasy 1 do kasy 2
        PrzeniesKlientowDoKasy2(g_stan_sklepu, g_sem_id, g_msg_id);
    } else {
        ZwolnijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
        ZapiszLog(LOG_OSTRZEZENIE, "Kierownik: Kasa 2 jest juz otwarta.");
    }
}

//Zamykanie kasy stacjonarnej
void ObslugaSIGUSR2(int sig) {
    (void)sig;

    if (!g_stan_sklepu) return;
    
    ZajmijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA, g_stan_sklepu);

    if (g_stan_sklepu->kasy_stacjonarne[0].stan != KASA_ZAMKNIETA) {

        g_stan_sklepu->kasy_stacjonarne[0].stan = KASA_ZAMYKANA; //Nowi klienci nie moga dolaczyc
        g_stan_sklepu->polecenie_kierownika = POLECENIE_ZAMKNIJ_KASE; //Przekazanie polecenia do kasjera
        g_stan_sklepu->id_kasy_do_zamkniecia = 0;
        ZwolnijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);

        ZapiszLog(LOG_INFO, "Kierownik: Sygnal SIGUSR2 => kasa 1 zamyka sie.");
    } else {
        ZwolnijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
        ZapiszLog(LOG_OSTRZEZENIE, "Kierownik: Kasa 1 jest juz zamknieta.");
    }
}

//Ewakuacja sklepu
void ObslugaSIGTERM(int sig) {
    (void)sig;
    ZapiszLog(LOG_OSTRZEZENIE, "Main: Otrzymano SIGTERM - Rozpoczynam procedure EWAKUACJI.");
    if (g_stan_sklepu) g_stan_sklepu->flaga_ewakuacji = 1;
}


//Handler SIGINT do czyszczenia zasobow => zamkniecie sklepu
void ObslugaSIGINT(int sig) {
    (void)sig;
    
    if (g_stan_sklepu) g_stan_sklepu->flaga_ewakuacji = 1; //Przerwanie petli glownej

    g_zadanie_zamkniecia = 1; //Flaga by wyjsc z petli glownej
    
    signal(SIGTERM, SIG_IGN); //Ignorowanie SIGTERM przez rodzica
    kill(0, SIGTERM); //Przekazanie sygnalu do wszystkich procesow potomnych
    signal(SIGTERM, ObslugaSIGTERM); //Przywrocenie sygnalu do swojej postaci
}



int main(int argc, char* argv[]) {
    srand(time(NULL));

    //Inicjalizacja potoku do informowania o zakonczeniu procesow potomnych
    if (pipe(g_pipe_fd) == -1) {
        perror("Blad pipe");
        return 1;
    }

    //Ustawienie odczytu na nieblokujacy
    int flags = fcntl(g_pipe_fd[0], F_GETFL, 0);
    fcntl(g_pipe_fd[0], F_SETFL, flags | O_NONBLOCK);
    
    //Flaga FD_CLOEXEC sprawia ze pipe nie bedzie dziedziczony przez execl()
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
    
    //Opcjonalny limit klientow rownoczesnie
    unsigned int max_klientow = MAX_KLIENTOW_ROWNOCZESNIE_DOMYSLNIE;
    if (argc >= 4) {
        max_klientow = atoi(argv[3]);
        if (max_klientow <= 0) {
            fprintf(stderr, "Blad: Maksymalna liczba klientow musi byc wieksza od 0\n");
            return 1;
        }
        if (max_klientow > 10000) {
            fprintf(stderr, "Blad: Maksymalna liczba klientow to 10000 (by nie przeciazyc systemu)\n");
            return 1;
        }
    }

    //Rejestracja handlerow sygnalow
    struct sigaction sa;
    sa.sa_handler = ObslugaSIGCHLD;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; //Sprawia ze select moze byc przerwany przez sygnaly
    sigaction(SIGCHLD, &sa, NULL); 

    signal(SIGINT, ObslugaSIGINT);    //Ctrl+C
    signal(SIGUSR1, ObslugaSIGUSR1);  //Otwieranie kasy 2
    signal(SIGUSR2, ObslugaSIGUSR2);  //Zamykanie kasy 1
    signal(SIGTERM, ObslugaSIGTERM);  //Ewakuacja
    signal(SIGQUIT, ObslugaSIGTERM);  //Ctrl+\: Ewakuacja
    
    
    //Inicjalizacja systemu logowania
    printf("=== Symulacja Dyskontu ===\n");
    printf("Czas symulacji: %d sekund\n", czas_symulacji);
    printf("Max klientow rownoczesnie: %u\n", max_klientow);
    if (tryb_testu == 1) {
        printf("TRYB TESTU: Bez sleepow symulacyjnych\n");
    }
    printf("PID glownego procesu: %d\n", getpid());

    InicjalizujSystemLogowania(argv[0]); //Inicjalizacja systemu logowania
    UruchomWatekLogujacy(); //Uruchomienie watku logujacego
    
    //Inicjalizacja pamieci wspoldzielonej
    ZapiszLog(LOG_INFO, "Inicjalizacja pamieci wspoldzielonej..");
    g_stan_sklepu = InicjalizujPamiecWspoldzielona();
    

    
    //Zapisz PID glownego procesu, tryb testu i max klientow do pamieci wspoldzielonej
    g_stan_sklepu->pid_glowny = getpid();
    g_stan_sklepu->tryb_testu = tryb_testu;
    g_stan_sklepu->max_klientow_rownoczesnie = max_klientow;
    
    ZapiszLog(LOG_INFO, "Pamiec wspoldzielona zainicjalizowana pomyslnie.");
    
    //Inicjalizacja wspólnej kolejki komunikatów dla kas samoobslugowych i kasjerow
    g_msg_id = msgget(GenerujKluczIPC(ID_IPC_KOLEJKA), IPC_CREAT | 0600);
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
    
    //Uruchomienie procesow kasjerow
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
            g_is_parent = 0; //Dziecko nie moze sprzatac zasobow
            
            char id_str[16];
            sprintf(id_str, "%d", i); //ID kasjera

            execl("./kasjer", "kasjer", id_str, (char*)NULL);
            
            perror("Blad exec() dla kasjera");
            exit(1);
        }
        else {
            //Przekazanie informacji o poprawnym uruchomieniu procesu kasjera
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
        g_is_parent = 0; //Dziecko nie moze sprzatac zasobow
        
        execl("./pracownik", "pracownik", (char*)NULL);
        
        perror("Blad exec() dla pracownika obslugi");
        exit(1);
    }
    else {
        //Przekazanie informacji o poprawnym uruchomieniu procesu pracownika obslugi
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
            g_is_parent = 0; //Dziecko nie moze sprzatac zasobow
            
            char id_str[16];
            sprintf(id_str, "%d", i); //ID kasy samoobslugowej
            
            execl("./kasa_samo", "kasa_samo", id_str, (char*)NULL);
            
            perror("Blad exec() dla kasy samoobslugowej");
            exit(1);
        }
        else {
            //Przekazanie informacji o poprawnym uruchomieniu procesu kasy samoobslugowej
            pid_kas_samo[i] = pid;
            char buf[256];
            sprintf(buf, "Uruchomiono proces kasy samoobslugowej [PID: %d, Kasa: %d]", pid, i + 1);
            ZapiszLog(LOG_INFO, buf);
        }
    }
    
    char buf[256];
    sprintf(buf, "Symulacja bedzie trwac %d sekund. Max klientow rownoczesnie: %u", 
            czas_symulacji, g_stan_sklepu->max_klientow_rownoczesnie);
    ZapiszLog(LOG_INFO, buf);
    
    //Zmienna dla tworzenia procesow klientow
    int id_klienta = 0;
    
    //Obliczenie czasu zakonczenia symulacji
    time_t czas_startu = time(NULL);
    time_t czas_konca = czas_startu + czas_symulacji;
    
    ZapiszLog(LOG_INFO, "Rozpoczynam ciagla symulacje klientow...");

    //START SYMULACJI
    while (time(NULL) < czas_konca && !g_stan_sklepu->flaga_ewakuacji && !g_zadanie_zamkniecia) {
        
        //Sprawdzamy pipe i aktualizujemy liczbe klientow i otwartych kas samoobslugowych
        AktualizujLicznikKlientow();
        ZaktualizujKasySamoobslugowe(g_stan_sklepu, g_sem_id, g_aktywnych_klientow);

        //Jezeli nie przekraczamy limitu klientow rownoczesnie, to tworzymy nowego klienta
        if (g_aktywnych_klientow < g_stan_sklepu->max_klientow_rownoczesnie) {
            pid_t pid = fork();
            
            if (pid == -1) {
                perror("Blad fork()");
                break;
            }
            else if (pid == 0) {
                g_is_parent = 0; //Dziecko nie sprzata zasobow

                char id_str[16];
                sprintf(id_str, "%d", id_klienta + 1);
                
                execl("./klient", "klient", id_str, "0", (char*)NULL);
                
                perror("Blad exec()");
                exit(1);
            }
            else {
                g_aktywnych_klientow++; //Reczne zwiekszenie liczby klientow przez rodzica
                id_klienta++;

                //Krotka przerwa miedzy tworzeniem procesow
                SYMULACJA_USLEEP(g_stan_sklepu, PRZERWA_MIEDZY_KLIENTAMI_MS * 1000);
            }
        } 
        else {
                //Czekamy na zakonczenie ktoregos z procesow klientow
                CzekajNaZdarzenieIPC(-1);
            }
    }
    
    //Sprawdz czy to bylo Ctrl+C czy skonczyl sie czas symulacji
    if (g_zadanie_zamkniecia) {
        ZapiszLog(LOG_INFO, "Otrzymano SIGINT - rozpoczynam zamykanie systemu..");
    }
    
    //Ustawienie flagi ewakuacji by zakonczyc dzialanie klientow
    ZajmijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA, g_stan_sklepu);
    g_stan_sklepu->flaga_ewakuacji = 1;
    ZwolnijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    
    //Wysylanie sygnalu zakonczenia do wszystkich procesow potomnych
    signal(SIGTERM, SIG_IGN); //Ignorowanie sygnalu przez rodzica
    kill(0, SIGTERM);
    signal(SIGTERM, ObslugaSIGTERM);  //Przywrocenie obslugi sygnalu
    
    sprintf(buf, "Koniec symulacji - ewakuacja. Czekam na %u klientow...", g_aktywnych_klientow);
    ZapiszLog(LOG_INFO, buf);
    
    //Czekanie na wyewakuowanie sie klientow
    time_t czas_oczekiwania_start = time(NULL);
    int timeout_klientow = 30;
    
    while (g_aktywnych_klientow > 0 && (time(NULL) - czas_oczekiwania_start) < timeout_klientow) {
        AktualizujLicznikKlientow();
        CzekajNaZdarzenieIPC(1); //Czekanie maksymalnie sekunde na zdarzenie wyjscia klienta
    }
    
    if (g_aktywnych_klientow > 0) {
        
        //Ponowne wyslanie sygnalu zakonczenia do wszystkich procesow potomnych
        signal(SIGTERM, SIG_IGN);
        kill(0, SIGTERM);
        signal(SIGTERM, ObslugaSIGTERM);
        
        ZapiszLog(LOG_INFO, "Czekam 5s na zakonczenie klientow po ewakuacji...");
        time_t start_ewak = time(NULL);
        while (g_aktywnych_klientow > 0 && (time(NULL) - start_ewak) < 5) {
            AktualizujLicznikKlientow();
            CzekajNaZdarzenieIPC(1);
        }
        
        if (g_aktywnych_klientow > 0) {
             ZapiszLog(LOG_BLAD, "Klienci nie chca wyjsc po dobroci. Zabijam procesy!");
             kill(0, SIGKILL);
        }
    }
    
    sprintf(buf, "Symulacja zakonczona. Laczna liczba klientow: %d", (int)g_calkowita_liczba_klientow);
    ZapiszLog(LOG_INFO, buf);
    
    //Zamykanie procesow pomocniczych
    ZapiszLog(LOG_INFO, "Zamykanie procesow kasjerow i kas samoobslugowych..");
    
    //Wyslij SIGTERM do wszystkich procesow pomocniczych aby je wybudzic z semaforow
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
            CzekajNaZdarzenieIPC(1);
        }
    }
    
    if (procesy_pozostale > 0) {
        
        //Ostateczne zabicie procesow jesli nie zakonczyly sie same
        for (int i = 0; i < LICZBA_KAS_STACJONARNYCH; i++) if (pid_kasjerow[i] > 0) kill(pid_kasjerow[i], SIGKILL);
        for (int i = 0; i < LICZBA_KAS_SAMOOBSLUGOWYCH; i++) if (pid_kas_samo[i] > 0) kill(pid_kas_samo[i], SIGKILL);
        if (pid_pracownik > 0) kill(pid_pracownik, SIGKILL);
        
        //Jeszcze chwila na posprzatanie przez OS
        sleep(1);
    }
    
    ZapiszLog(LOG_INFO, "Koniec symulacji.");
    
    //Czyszczenie zasobow IPC
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
        
        //Zamkniecie loggera na samym koncu by wyswietlic wszystkie logi
        ZamknijSystemLogowania();
    }
    
    if (g_is_parent) {
        if (g_zadanie_zamkniecia) {
            printf("\n=== System zamkniety (przez Ctrl+C) ===\n");
        } else {
            printf("\n=== Symulacja zakonczona ===\n");
        }
    }
    return 0;
}