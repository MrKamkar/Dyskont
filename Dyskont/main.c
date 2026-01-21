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
#include "wspolne.h"
#include "kolejki.h"
#include "kasjer.h"

static StanSklepu* g_stan_sklepu = NULL; //Wskaznik do pamieci wspoldzielonej
static int g_sem_id = -1; //ID tablicy semaforow
static int g_msg_id_1 = -1;
static int g_msg_id_2 = -1;
static int g_msg_id_samo = -1;
static int g_msg_id_prac = -1;

static int g_czy_rodzic = 1; //Flaga mowiaca czy jestesmy w glownym procesie

//PIDy procesow potomnych
static pid_t g_pid_kasjerow[LICZBA_KAS_STACJONARNYCH];
static pid_t g_pid_manager_samoobslugowych;
static pid_t g_pid_pracownika;
static pid_t g_pid_generatora_klientow;

//Funkcja sprzatajaca zasoby IPC
static void PosprzatajZasobyIPC() {
    ZapiszLog(LOG_INFO, "Zwalnienie zasobow IPC.");

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
    UsunKolejke(g_msg_id_samo);
    UsunKolejke(g_msg_id_prac);
    
    ZapiszLog(LOG_INFO, "Usuniecie kolejki komunikatow.");
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
    } else if (wynik == -1 && errno == ECHILD) ZapiszLogF(LOG_INFO, "Proces generatora klientow zostal juz zakonczony wczesniej.");

    //Czyszczenie kasjerow
    for (int i = 0; i < LICZBA_KAS_STACJONARNYCH; i++) {
        kill(g_pid_kasjerow[i], SIGTERM);
        wynik = waitpid(g_pid_kasjerow[i], &status, 0);
        if (wynik > 0) {
            if (WIFEXITED(status)) ZapiszLogF(LOG_INFO, "Kasjer [Kasa %d, PID: %d] zakonczony (status: %d)", i + 1, wynik, WEXITSTATUS(status));
            else if (WIFSIGNALED(status)) ZapiszLogF(LOG_INFO, "Kasjer [Kasa %d, PID: %d] zabity sygnalem %d", i + 1, wynik, WTERMSIG(status));
        } else if (wynik == -1 && errno == ECHILD) ZapiszLogF(LOG_INFO, "Kasjer [Kasa %d] zostal juz zakonczony wczesniej.", i + 1);
    }

    //Czyszczenie kas samoobslugowych
    kill(g_pid_manager_samoobslugowych, SIGTERM);
    wynik = waitpid(g_pid_manager_samoobslugowych, &status, 0);
    if (wynik > 0) {
        if (WIFEXITED(status)) ZapiszLogF(LOG_INFO, "Manager kas samoobslugowych [PID: %d] zakonczony (status: %d)", wynik, WEXITSTATUS(status));
        else if (WIFSIGNALED(status)) ZapiszLogF(LOG_INFO, "Manager kas samoobslugowych [PID: %d] zabity sygnalem %d", wynik, WTERMSIG(status));
    } else if (wynik == -1 && errno == ECHILD) ZapiszLogF(LOG_INFO, "Manager kas samoobslugowych zostal juz zakonczony wczesniej.");

    //Czyszczenie pracownika
    kill(g_pid_pracownika, SIGTERM);
    wynik = waitpid(g_pid_pracownika, &status, 0);
    if (wynik > 0) {
        if (WIFEXITED(status)) ZapiszLogF(LOG_INFO, "Pracownik obslugi [PID: %d] zakonczony (status: %d)", wynik, WEXITSTATUS(status));
        else if (WIFSIGNALED(status)) ZapiszLogF(LOG_INFO, "Pracownik obslugi [PID: %d] zabity sygnalem %d", wynik, WTERMSIG(status));
    } else if (wynik == -1 && errno == ECHILD) ZapiszLogF(LOG_INFO, "Pracownik obslugi zostal juz zakonczony wczesniej.");
    
    return NULL;
}

//Sygnal do otwarcia kasy stacjonarnej 2
void ObslugaSIGUSR1(int sig) {
    (void)sig;

    if (!g_stan_sklepu) return;
    
    ZajmijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    if (g_stan_sklepu->kasy_stacjonarne[1].stan == KASA_ZAMKNIETA) {

        g_stan_sklepu->kasy_stacjonarne[1].stan = KASA_WOLNA;
        ZwolnijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
        
        //Sygnal dla kasjera ze kasa jest juz otwarta
        ZwolnijSemafor(g_sem_id, SEM_OTWORZ_KASA_STACJONARNA_2);
        
        ZapiszLog(LOG_INFO, "Kierownik: Sygnal SIGUSR1 => otwarto kase stacjonarna 2.");
        
        //Migracja klientow z kasy 1 do kasy 2
        //PrzeniesKlientowDoKasy2(g_stan_sklepu, g_sem_id, g_msg_id);
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

    //Pobranie kasy do zamkniecia
    int id_kasy_do_zamkniecia = g_stan_sklepu->id_kasy_do_zamkniecia;
    Kasa kasa_do_zamkniecia = g_stan_sklepu->kasy_stacjonarne[id_kasy_do_zamkniecia];

    //Zamykanie kasy stacjonarnej
    if (kasa_do_zamkniecia.stan != KASA_ZAMKNIETA) {

        kasa_do_zamkniecia.stan = KASA_ZAMYKANA; //Nowi klienci nie moga dolaczyc
        kill(g_pid_kasjerow[id_kasy_do_zamkniecia], SIGUSR1);
        ZwolnijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);

        ZapiszLogF(LOG_INFO, "Kierownik: Sygnal SIGUSR2 => kasa %d zamyka sie.", id_kasy_do_zamkniecia + 1);
    } else {
        ZwolnijSemafor(g_sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
        ZapiszLogF(LOG_OSTRZEZENIE, "Kierownik: Kasa %d jest juz zamknieta.", id_kasy_do_zamkniecia + 1);
    }
}

void ObslugaSIGTERM(int sig) {
    signal(SIGTERM, SIG_IGN);  //Ignorowanie SIGTERM w main

    (void)sig;
    ZapiszLog(LOG_OSTRZEZENIE, "Main: Otrzymano SIGTERM - Rozpoczynam procedure EWAKUACJI.");
    
    pthread_t czyszczenie;
    if (pthread_create(&czyszczenie, NULL, WatekSprzatajacy, NULL) != 0) {
        perror("Blad tworzenia watku sprzatajacego");
        WatekSprzatajacy(NULL); //Uruchomienie funkcji asynchronicznie
    } else pthread_join(czyszczenie, NULL);
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
    g_stan_sklepu = InicjalizujPamiecWspoldzielona();

    //Zapisz PID glownego procesu, tryb testu i max klientow do pamieci wspoldzielonej
    g_stan_sklepu->pid_glowny = getpid();
    g_stan_sklepu->tryb_testu = tryb_testu;
    g_stan_sklepu->max_klientow_rownoczesnie = max_klientow;

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
    g_msg_id_samo = StworzKolejke(ID_IPC_SAMO);
    g_msg_id_prac = StworzKolejke(ID_IPC_PRACOWNIK);

    //Ograniczamy pojemnosc kolejek komunikatow o 1 miejsce, aby uniknac deadloku
    ZostawMiejsceWKolejce(g_msg_id_1, sizeof(MsgKasaStacj) - sizeof(long));
    ZostawMiejsceWKolejce(g_msg_id_2, sizeof(MsgKasaStacj) - sizeof(long));
    ZostawMiejsceWKolejce(g_msg_id_samo, sizeof(MsgKasaSamo) - sizeof(long));
    ZostawMiejsceWKolejce(g_msg_id_prac, sizeof(MsgPracownik) - sizeof(long));

    if (g_msg_id_1 == -1 || g_msg_id_2 == -1 || g_msg_id_samo == -1 || g_msg_id_prac == -1) {
        perror("Blad tworzenia kolejek komunikatow");
        ZapiszLog(LOG_BLAD, "Nie udalo sie stworzyc kolejek komunikatow!");

        ObslugaSIGTERM(0);
        return 1;
    }
    ZapiszLog(LOG_INFO, "Kolejki komunikatow zainicjalizowane.");

    //URUCHOMIENIE PROCESOW KASJEROW
    for (int i = 0; i < LICZBA_KAS_STACJONARNYCH; i++) {
        g_pid_kasjerow[i] = fork();
        
        if (g_pid_kasjerow[i] == -1) {
            perror("Blad fork() dla kasjera");
            ZapiszLog(LOG_BLAD, "Nie udalo sie uruchomic procesu kasjera!");
            ObslugaSIGTERM(0);
            PosprzatajZasobyIPC();
            return 1;
        }
        if (g_pid_kasjerow[i] == 0) {
            g_czy_rodzic = 0; //Dziecko nie moze sprzatac zasobow

            char id_str[16];
            sprintf(id_str, "%d", i); //ID kasjera

            execl("./kasjer", "kasjer", id_str, (char*)NULL);
            
            perror("Blad exec() dla kasjera");
            ObslugaSIGTERM(0);
            return 1;
        }
        else {
            //Przekazanie informacji o poprawnym uruchomieniu procesu kasjera
            ZapiszLogF(LOG_INFO, "Uruchomiono proces kasjera [PID: %d, Kasa: %d]", g_pid_kasjerow[i], i + 1);
        }
    }

    //URUCHOMIENIE MANAGERA KAS SAMOOBSLUGOWYCH
    g_pid_manager_samoobslugowych = fork();
    if (g_pid_manager_samoobslugowych == -1) {
        perror("Blad fork() dla managera kas samoobslugowych");
        ObslugaSIGTERM(0);
        PosprzatajZasobyIPC();
        return 1;
    }
    else if (g_pid_manager_samoobslugowych == 0) {
        g_czy_rodzic = 0;

        execl("./kasa_samoobslugowa", "kasa_samoobslugowa", (char*)NULL);

        perror("Blad exec() dla managera kas samoobslugowych");
        ObslugaSIGTERM(0);
        exit(1);
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
        PosprzatajZasobyIPC();
        return 1;
    }
    else if (g_pid_pracownika == 0) {
        g_czy_rodzic = 0; //Dziecko nie moze sprzatac zasobow
        
        execl("./pracownik", "pracownik", (char*)NULL);
        
        perror("Blad exec() dla pracownika obslugi");
        ObslugaSIGTERM(0);
        return 1;
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
        ObslugaSIGTERM(0);
        return 1;
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

    //Sprzatanie IPC
    PosprzatajZasobyIPC();

    printf("=== Koniec symulacji ===\n");
    return 0;
}