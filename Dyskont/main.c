#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <pthread.h>
#include <errno.h>
#include "logi.h"
#include "pamiec_wspoldzielona.h"
#include "semafory.h"
#include "pamiec_wspoldzielona.h"
#include "kolejki.h"
#include "kasjer.h"

//Globalne zmienne
static StanSklepu* g_stan_sklepu = NULL; //Wskaznik do pamieci wspoldzielonej
static int g_sem_id = -1; //ID tablicy semaforow
static int g_msg_id_1 = -1;
static int g_msg_id_2 = -1;
static int g_msg_id_wspolna = -1;
static int g_msg_id_samo = -1;
static int g_msg_id_prac = -1;

static int g_czy_rodzic = 1; //Flaga mowiaca czy jestesmy w glownym procesie

//PIDy procesow potomnych
static pid_t g_pid_manager_kasjerow;
static pid_t g_pid_manager_samoobslugowych;
static pid_t g_pid_pracownika;
static pid_t g_pid_generatora_klientow;

//Funkcja sprzatajaca zasoby IPC
static void PosprzatajZasobyIPC() {
    ZapiszLog(LOG_INFO, "Zwalnienie zasobow IPC");

    if (g_stan_sklepu) {
        OdlaczPamiecWspoldzielona(g_stan_sklepu);
        g_stan_sklepu = NULL;
    }

    UsunPamiecWspoldzielona();

    //Usuwanie semaforow
    UsunSemafory(g_sem_id);

    //Usuwanie kolejek komunikatow
    UsunKolejke(g_msg_id_1);
    UsunKolejke(g_msg_id_2);
    UsunKolejke(g_msg_id_wspolna);
    UsunKolejke(g_msg_id_samo);
    UsunKolejke(g_msg_id_prac);
    
    ZapiszLog(LOG_INFO, "Usuniecie kolejki komunikatow");
    ZamknijSystemLogowania();
}

//Watek sprzatajacy
void* WatekSprzatajacy(void* arg) {
    (void)arg;
    pid_t wynik;

    //Tylko proces glowny moze sprzatac zasoby systemowe
    if (!g_czy_rodzic) {
        if(g_stan_sklepu) UsunPamiecWspoldzielona();
        _exit(1);
    }

    int status;
    
    //Czyszczenie generatora klientow
    kill(g_pid_generatora_klientow, SIGTERM);
    wynik = waitpid(g_pid_generatora_klientow, &status, 0);
    if (wynik > 0) {
        if (WIFEXITED(status)) ZapiszLogF(LOG_INFO, "Generator klientow [PID: %d] zakonczony (status: %d)", wynik, WEXITSTATUS(status));
        else if (WIFSIGNALED(status)) ZapiszLogF(LOG_INFO, "Generator klientow [PID: %d] zabity sygnalem %d", wynik, WTERMSIG(status));
    } else if (wynik == -1 && errno == ECHILD) ZapiszLogF(LOG_INFO, "Proces generatora klientow zostal juz zakonczony wczesniej");

    //Czyszczenie managera kas stacjonarnych
    kill(g_pid_manager_kasjerow, SIGTERM);
    wynik = waitpid(g_pid_manager_kasjerow, &status, 0);
    if (wynik > 0) {
        if (WIFEXITED(status)) ZapiszLogF(LOG_INFO, "Manager kas stacjonarnych [PID: %d] zakonczony (status: %d)", wynik, WEXITSTATUS(status));
        else if (WIFSIGNALED(status)) ZapiszLogF(LOG_INFO, "Manager kas stacjonarnych [PID: %d] zabity sygnalem %d", wynik, WTERMSIG(status));
    } else if (wynik == -1 && errno == ECHILD) ZapiszLogF(LOG_INFO, "Manager kas stacjonarnych zostal juz zakonczony wczesniej");

    //Czyszczenie kas samoobslugowych
    kill(g_pid_manager_samoobslugowych, SIGTERM);
    wynik = waitpid(g_pid_manager_samoobslugowych, &status, 0);
    if (wynik > 0) {
        if (WIFEXITED(status)) ZapiszLogF(LOG_INFO, "Manager kas samoobslugowych [PID: %d] zakonczony (status: %d)", wynik, WEXITSTATUS(status));
        else if (WIFSIGNALED(status)) ZapiszLogF(LOG_INFO, "Manager kas samoobslugowych [PID: %d] zabity sygnalem %d", wynik, WTERMSIG(status));
    } else if (wynik == -1 && errno == ECHILD) ZapiszLogF(LOG_INFO, "Manager kas samoobslugowych zostal juz zakonczony wczesniej");

    //Czyszczenie pracownika
    kill(g_pid_pracownika, SIGTERM);
    wynik = waitpid(g_pid_pracownika, &status, 0);
    if (wynik > 0) {
        if (WIFEXITED(status)) ZapiszLogF(LOG_INFO, "Pracownik obslugi [PID: %d] zakonczony (status: %d)", wynik, WEXITSTATUS(status));
        else if (WIFSIGNALED(status)) ZapiszLogF(LOG_INFO, "Pracownik obslugi [PID: %d] zabity sygnalem %d", wynik, WTERMSIG(status));
    } else if (wynik == -1 && errno == ECHILD) ZapiszLogF(LOG_INFO, "Pracownik obslugi zostal juz zakonczony wczesniej");
    
    //Czyszczenie kierownika (jesli uruchomiony)
    if (g_stan_sklepu->pid_kierownika > 0) {
        kill(g_stan_sklepu->pid_kierownika, SIGTERM);
        ZapiszLogF(LOG_INFO, "Wyslano SIGTERM do kierownika [PID: %d]", g_stan_sklepu->pid_kierownika);
    }

    return NULL;
}

//Sygnal do otwarcia kasy stacjonarnej 2 - przekazanie do procesu kasjer
void ObslugaSIGUSR1(int sig) {
    (void)sig;

    if (!g_stan_sklepu) return;
    
    //Przekazujemy sygnal do procesu managera kas stacjonarnych
    if (g_pid_manager_kasjerow > 0) {
        kill(g_pid_manager_kasjerow, SIGUSR1);
        ZapiszLog(LOG_INFO, "Main: Przekazano SIGUSR1 do managera kas stacjonarnych (otwieranie kasy 2)");
    }
}

//Zamykanie kasy stacjonarnej - przekazanie do procesu kasjer
void ObslugaSIGUSR2(int sig) {
    (void)sig;

    if (!g_stan_sklepu) return;
    
    //Przekazujemy sygnal do procesu managera kas stacjonarnych
    if (g_pid_manager_kasjerow > 0) {
        kill(g_pid_manager_kasjerow, SIGUSR2);
        ZapiszLog(LOG_INFO, "Main: Przekazano SIGUSR2 do managera kas stacjonarnych (zamykanie kasy)");
    }
}

void ObslugaSIGTERM(int sig) {
    signal(SIGTERM, SIG_IGN);  //Ignorowanie SIGTERM w main

    (void)sig;
    ZapiszLog(LOG_OSTRZEZENIE, "Main: Otrzymano SIGTERM - Rozpoczynam procedure EWAKUACJI");
    
    pthread_t czyszczenie;
    if (pthread_create(&czyszczenie, NULL, WatekSprzatajacy, NULL) != 0) {
        perror("Blad tworzenia watku sprzatajacego");
        WatekSprzatajacy(NULL); //Uruchomienie funkcji asynchronicznie
    } else pthread_join(czyszczenie, NULL);

    PosprzatajZasobyIPC();
}



int main(int argc, char* argv[]) {

    //Pobranie czasu symulacji i opcjonalnych argumentow
    if (argc <= 2) {
        fprintf(stderr, "Uzycie: %s <pula_klientow> <max_klientow_sklep> <nr_testu*>\n", argv[0]);
        fprintf(stderr, "<pula_klientow> - calkowita liczba klientow do stworzenia\n");
        fprintf(stderr, "<max_klientow> - max klientow w sklepie rownoczesnie\n");
        fprintf(stderr, "<nr_testu*> - tryb testu (0=normalny, 1=bez sleepow), domyslnie 0\n");
        
        return 1;
    }
    
    //Pula klientow
    int pula_klientow = atoi(argv[1]);
    if (pula_klientow <= 0) {
        fprintf(stderr, "Blad: Pula klientow musi byc wieksza od 0\n");
        return 1;
    }

    //Ilosc klientow w sklepie rownoczesnie
    int max_klientow = atoi(argv[2]);
    if (max_klientow <= 0) {
        fprintf(stderr, "Blad: Ilosc klientow w sklepie rownoczesnie nie moze byc mniejsza niz 0\n");
        return 1;
    }
    
    //Ograniczenie ze wzgledu na limity semaforow
    if (max_klientow > 32000) {
        printf("Ostrzezenie: Liczba klientow w sklepie (%d) przekracza limit semafora. Ustawiam na 32000\n", max_klientow);
        max_klientow = 32000;
    }

    //Opcjonalny tryb testu
    int tryb_testu = 0;
    if (argc >= 4) {
        tryb_testu = atoi(argv[3]);
        if (tryb_testu < 0 || tryb_testu > 1) {
            fprintf(stderr, "Blad: Nieprawidlowy tryb testu (dozwolone: 0, 1)\n");
            return 1;
        }
    }


    //Obsluga sygnalow
    signal(SIGINT, ObslugaSIGTERM);    //Ctrl+C
    signal(SIGUSR1, ObslugaSIGUSR1);  //Otwieranie kasy 2
    signal(SIGUSR2, ObslugaSIGUSR2);  //Zamykanie kasy 1
    signal(SIGTERM, ObslugaSIGTERM);  //Ewakuacja
    signal(SIGQUIT, ObslugaSIGTERM);  //Ctrl+\: Ewakuacja


    //Rozpoczecie symulacji
    printf("=== Symulacja Dyskontu ===\n");
    printf("PID glownego procesu: %d\n", getpid());
    printf("Pula klientow chcacych wejsc do sklepu: %d\n", pula_klientow);
    printf("Max klientow rownoczesnie: %u\n", max_klientow);
    if (tryb_testu == 1) {
        printf("TRYB TESTU: Bez sleepow symulacyjnych\n");
    }

    InicjalizujSystemLogowania(argv[0]); //Inicjalizacja systemu logowania
    UruchomWatekLogujacy(); //Uruchomienie watku logujacego

    //Inicjalizacja pamieci wspoldzielonej
    ZapiszLog(LOG_INFO, "Inicjalizacja pamieci wspoldzielonej..");
    g_stan_sklepu = InicjalizujPamiecWspoldzielona(max_klientow);

    //Zapisz PID glownego procesu i tryb testu do pamieci wspoldzielonej
    g_stan_sklepu->pid_glowny = getpid();
    g_stan_sklepu->tryb_testu = tryb_testu;

    ZapiszLog(LOG_INFO, "Pamiec wspoldzielona zainicjalizowana pomyslnie.");

    //Inicjalizacja semaforow
    ZapiszLog(LOG_INFO, "Inicjalizacja semaforow..");
    g_sem_id = InicjalizujSemafory(max_klientow);

    if (g_sem_id == -1) {
        perror("Blad inicjalizacji semaforow");
        ZapiszLog(LOG_BLAD, "Nie udalo sie zainicjalizowac semaforow!");
        ObslugaSIGTERM(0);
        return 1;
    }
    ZapiszLog(LOG_INFO, "Semafory zainicjalizowane pomyslnie.");

    //Inicjalizacja kolejek komunikatow
    g_msg_id_1 = StworzKolejke(ID_IPC_KASA_1);
    g_msg_id_2 = StworzKolejke(ID_IPC_KASA_2);
    g_msg_id_wspolna = StworzKolejke(ID_IPC_KASA_WSPOLNA);
    g_msg_id_samo = StworzKolejke(ID_IPC_SAMO);
    g_msg_id_prac = StworzKolejke(ID_IPC_PRACOWNIK);
    
    if (g_msg_id_1 == -1 || g_msg_id_2 == -1 || g_msg_id_wspolna == -1 || g_msg_id_samo == -1 || g_msg_id_prac == -1) {
        perror("Blad tworzenia kolejek komunikatow");
        ZapiszLog(LOG_BLAD, "Nie udalo sie stworzyc kolejek komunikatow!");

        ObslugaSIGTERM(0);
        return 1;
    }
    ZapiszLog(LOG_INFO, "Kolejki komunikatow zainicjalizowane.");

    //URUCHOMIENIE MANAGERA KAS STACJONARNYCH (Kasa 1 + watek zarzadzajacy)
    g_pid_manager_kasjerow = fork();
    
    if (g_pid_manager_kasjerow == -1) {
        perror("Blad fork() dla managera kas stacjonarnych");
        ZapiszLog(LOG_BLAD, "Nie udalo sie uruchomic procesu managera kas stacjonarnych!");
        ObslugaSIGTERM(0);
        return 1;
    }
    if (g_pid_manager_kasjerow == 0) {
        g_czy_rodzic = 0; //Dziecko nie moze sprzatac zasobow

        execl("./kasjer", "kasjer", (char*)NULL);
        
        perror("Blad exec() dla managera kas stacjonarnych");
        kill(getppid(), SIGTERM);
        _exit(1);
    }
    else {
        //Przekazanie informacji o poprawnym uruchomieniu procesu managera kas stacjonarnych
        ZapiszLogF(LOG_INFO, "Uruchomiono proces managera kas stacjonarnych [PID: %d]", g_pid_manager_kasjerow);
    }

    //URUCHOMIENIE MANAGERA KAS SAMOOBSLUGOWYCH
    g_pid_manager_samoobslugowych = fork();
    if (g_pid_manager_samoobslugowych == -1) {
        perror("Blad fork() dla managera kas samoobslugowych");
        ObslugaSIGTERM(0);
        return 1;
    }
    else if (g_pid_manager_samoobslugowych == 0) {
        g_czy_rodzic = 0;

        execl("./kasa_samoobslugowa", "kasa_samoobslugowa", (char*)NULL);

        perror("Blad exec() dla managera kas samoobslugowych");
        kill(getppid(), SIGTERM);
        _exit(1);
    }
    else {
        //Przekazanie informacji o poprawnym uruchomieniu procesu managera kas samoobslugowych
        ZapiszLogF(LOG_INFO, "Uruchomiono proces managera kas samoobslugowych [PID: %d]", g_pid_manager_samoobslugowych);
    }

    //URUCHOMIENIE PROCESU PRACOWNIKA OBSLUGI
    ZapiszLog(LOG_INFO, "Uruchamianie procesu pracownika obslugi..");
    
    g_pid_pracownika = fork();
    if (g_pid_pracownika == -1) {
        perror("Blad fork() dla pracownika obslugi");
        ZapiszLog(LOG_BLAD, "Nie udalo sie stworzyc procesu pracownika obslugi!");
        ObslugaSIGTERM(0);
        return 1;
    }
    else if (g_pid_pracownika == 0) {
        g_czy_rodzic = 0; //Dziecko nie moze sprzatac zasobow
        
        execl("./pracownik", "pracownik", (char*)NULL);
        
        perror("Blad exec() dla pracownika obslugi");
        kill(getppid(), SIGTERM);
        _exit(1);
    }
    else {
        //Przekazanie informacji o poprawnym uruchomieniu procesu pracownika obslugi
        ZapiszLogF(LOG_INFO, "Uruchomiono proces pracownika obslugi [PID: %d]", g_pid_pracownika);
    }

    //URUCHOMIENIE PROCESU GENERATORA KLIENCIOW
    g_pid_generatora_klientow = fork();
    if (g_pid_generatora_klientow == -1) {
        perror("Blad fork() dla klienta");
        ObslugaSIGTERM(0);
        PosprzatajZasobyIPC();
        return 1;
    }
    else if (g_pid_generatora_klientow == 0) {
        g_czy_rodzic = 0; //Dziecko nie moze sprzatac zasobow

        char bufor[16];
        sprintf(bufor, "%d", pula_klientow); //Konwersja int na string
        execl("./klient", "klient", bufor, "-quiet", (char*)NULL);
        
        perror("Blad exec() dla klienta");
        kill(getppid(), SIGTERM);
        _exit(1);
    }
    else {
        //Przekazanie informacji o poprawnym uruchomieniu procesu generatora klientow
        ZapiszLogF(LOG_INFO, "Uruchomiono proces generatora klientow [PID: %d]", g_pid_generatora_klientow);
    }

    //Glowna petla oczekiwania na zakonczenie procesow potomnych
    pid_t pid_wait;
    int status;
    while (1) {
        pid_wait = wait(&status);
        if (pid_wait > 0) {
            if (WIFEXITED(status)) {
                ZapiszLogF(LOG_INFO, "Proces potomny [PID: %d] zakonczyl dzialanie (status: %d)", pid_wait, WEXITSTATUS(status));
            } else if (WIFSIGNALED(status)) {
                ZapiszLogF(LOG_INFO, "Proces potomny [PID: %d] zostal zabity sygnalem %d", pid_wait, WTERMSIG(status));
            }
        }
        else {
            if (errno == ECHILD) {
                break;
            }
        }
    }

    printf("=== Koniec symulacji ===\n");
    return 0;
}
