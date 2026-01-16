#include "logi.h"
#include "pamiec_wspoldzielona.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdarg.h>
#include <signal.h>

static int id_kolejki = -1;
static pthread_t watek_loggera;
static int watek_uruchomiony = 0;

//Petla watku logujacego
static void* PetlaLoggera(void* arg) {
    (void)arg;

    //Zablokuj wszystkie sygnaly w watku loggera (zeby Main je obslugiwal)
    sigset_t set;
    sigfillset(&set);
    pthread_sigmask(SIG_BLOCK, &set, NULL);
    
    mkdir("logs", 0300);
    
    //Pobranie aktualnego czasu
    time_t teraz = time(NULL);
    struct tm *czas = localtime(&teraz);
    
    char sciezka[64];
    sprintf(sciezka, "logs/dyskont_%04d-%02d-%02d_%02d-%02d.log",
        czas->tm_year + 1900,
        czas->tm_mon + 1,
        czas->tm_mday,
        czas->tm_hour,
        czas->tm_min);
    
    int deskryptor_pliku = open(sciezka, O_WRONLY | O_CREAT, 0200);
    if (deskryptor_pliku == -1) {
        perror("Blad otwarcia pliku logow");
        pthread_exit(NULL);
    }

    struct KomunikatLog msg;
    while (1) {
        ssize_t result = msgrcv(id_kolejki, &msg, sizeof(msg) - sizeof(long), 0, 0);
        if (result == -1) {
            //Jesli wywolanie zostalo przerwane przez sygnal, ponow je
            if (errno == EINTR) {
                continue;
            }
            perror("Blad msgrcv");
            break;
        }

        //Sprawdzenie czy to sygnal konca
        if (msg.typ_komunikatu == TYP_KONIEC) {
            break; 
        }

        //Logika formatowania
        const char* prefix = "";
        const char* kolor = KOLOR_RESET;
        
        switch (msg.typ_logu) {
            case LOG_INFO:
                prefix = "[INFO] ";
                kolor = KOLOR_ZIELONY;
                break;
            case LOG_OSTRZEZENIE:
                prefix = "[OSTRZ] ";
                kolor = KOLOR_ZOLTY;
                break;
            case LOG_BLAD:
                prefix = "[BLAD] ";
                kolor = KOLOR_CZERWONY;
                break;
            case LOG_DEBUG:
                prefix = "[DEBUG] ";
                kolor = KOLOR_NIEBIESKI;
                break;
            default:
                prefix = "[LOG] ";
                kolor = KOLOR_RESET;
                break;
        }

        //Czas
        teraz = time(NULL);
        czas = localtime(&teraz);

        char czas_buf[16];
        sprintf(czas_buf, "[%02d:%02d:%02d] ", czas->tm_hour, czas->tm_min, czas->tm_sec);

        //Wyjscie na ekran z kolorami
        printf("%s%s%s%s%s\n", czas_buf, kolor, prefix, msg.tresc, KOLOR_RESET);
        fflush(stdout);  //Wymuszenie wypisania

        //Zapis do pliku
        char bufor_pliku[512];
        sprintf(bufor_pliku, "%s%s%s\n", czas_buf, prefix, msg.tresc);
        if (write(deskryptor_pliku, bufor_pliku, strlen(bufor_pliku)) == -1) {
            perror("Blad zapisu do pliku logow");
        }
    }

    close(deskryptor_pliku);
    pthread_exit(NULL);
}

void InicjalizujSystemLogowania() {
    
    key_t klucz = ftok(IPC_SCIEZKA, 65); //'A' = 65
    if (klucz == -1) {
        perror("Blad generowania klucza");
        exit(1);
    }

    //Inicjalizacja IPC
    id_kolejki = msgget(klucz, IPC_CREAT | 0600);
    if (id_kolejki == -1) {
        perror("Blad tworzenia kolejki komunikatow");
        exit(1);
    }
}

void UruchomWatekLogujacy() {
    if (pthread_create(&watek_loggera, NULL, PetlaLoggera, NULL) != 0) {
        perror("Blad pthread_create");
        exit(1);
    }
    watek_uruchomiony = 1;
}

void ZamknijSystemLogowania() {
    //Zamiast wysylac komunikat (co moze blokowac gdy kolejka pelna), anulujemy watek
    
    //Czekanie na zakonczenie watku loggera (tylko jesli to nie logger wola te funkcje)
    if (watek_uruchomiony) {
        if (!pthread_equal(pthread_self(), watek_loggera)) {
            //Wymus zakonczenie (cancellation)
            pthread_cancel(watek_loggera);
            
            if (pthread_join(watek_loggera, NULL) != 0) {
                perror("Blad pthread_join");
            }
        }
    }

    //Usuniecie kolejki
    if (msgctl(id_kolejki, IPC_RMID, NULL) == -1) {
        perror("Blad usuwania kolejki");
    }
}

void ZapiszLog(TypLogu typ_logu, const char* format) {
    if (id_kolejki == -1) return;

    struct KomunikatLog msg;
    msg.typ_komunikatu = 1; //Zwykly komunikat
    msg.typ_logu = typ_logu;
    strncpy(msg.tresc, format, 127);
    msg.tresc[127] = '\0';

    //Wyslanie do kolejki bez blokowania
    msgsnd(id_kolejki, &msg, sizeof(msg) - sizeof(long), IPC_NOWAIT);
    //Bledy ignorowane - kolejka moze byc usunieta podczas zamykania
}

void ZapiszLogF(TypLogu typ_logu, const char* format, ...) {
    if (id_kolejki == -1) return;

    struct KomunikatLog msg;
    msg.typ_komunikatu = 1;
    msg.typ_logu = typ_logu;
    
    va_list args;
    va_start(args, format);
    vsnprintf(msg.tresc, sizeof(msg.tresc), format, args);
    va_end(args);

    msgsnd(id_kolejki, &msg, sizeof(msg) - sizeof(long), IPC_NOWAIT);
}
