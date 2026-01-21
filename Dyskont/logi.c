#include "logi.h"
#include "pamiec_wspoldzielona.h"
#include "wspolne.h"
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

    //Zablokuj wszystkie sygnaly w watku loggera by to proces glowny je obslugiwal
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
    
    int deskryptor_pliku = open(sciezka, O_WRONLY | O_CREAT, 0600);
    if (deskryptor_pliku == -1) {
        perror("Blad otwarcia pliku logow");
        pthread_exit(NULL);
    }
    
    //Ustawienie flagi FD_CLOEXEC zeby plik logu nie byl dziedziczony przez execl()
    fcntl(deskryptor_pliku, F_SETFD, FD_CLOEXEC);

    struct KomunikatLog msg;
    while (1) {
        ssize_t result = msgrcv(id_kolejki, &msg, sizeof(msg) - sizeof(long), 0, 0);
        if (result == -1) {
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

    //Zamykamy plik logow oraz konczymy watek
    close(deskryptor_pliku);
    pthread_exit(NULL);
}

void InicjalizujSystemLogowania() {
    
    key_t klucz = GenerujKluczIPC(ID_IPC_LOGI);
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

//Wyslanie komunikatu konca i czekanie az watek przetworzy wszystkie logi
void ZamknijSystemLogowania() {

    if (watek_uruchomiony && id_kolejki != -1) {
        if (!pthread_equal(pthread_self(), watek_loggera)) {
            //Wyslij komunikat konca
            struct KomunikatLog msg;
            msg.typ_komunikatu = TYP_KONIEC;
            msg.typ_logu = LOG_INFO;
            strcpy(msg.tresc, "Koniec logowania");
            msgsnd(id_kolejki, &msg, sizeof(msg) - sizeof(long), 0); //Blokujace
            
            //Czekaj na zakonczenie watku loggera
            pthread_join(watek_loggera, NULL);
        }
    }

    //Usuniecie kolejki
    if (id_kolejki != -1 && msgctl(id_kolejki, IPC_RMID, NULL) == -1) {
        perror("Blad usuwania kolejki logow");
    }
}

void ZapiszLog(TypLogu typ_logu, const char* format) {
    if (id_kolejki == -1) return;

    struct KomunikatLog msg;
    msg.typ_komunikatu = 1; //Zwykly komunikat
    msg.typ_logu = typ_logu;
    strncpy(msg.tresc, format, 127);
    msg.tresc[127] = '\0';

    //Wyslanie do kolejki (nieblokujace by logi nie zatrzymywaly symulacji)
    msgsnd(id_kolejki, &msg, sizeof(msg) - sizeof(long), 0);
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

    //Wyslanie do kolejki (nieblokujace by logi nie zatrzymywaly symulacji)
    msgsnd(id_kolejki, &msg, sizeof(msg) - sizeof(long), 0);
}
