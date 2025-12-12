#include "logi.h"
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <stdlib.h>
#include <sys/wait.h>

static int id_kolejki = -1;
static pid_t pid_loggera = -1;

// Petla procesu logujacego
static void PetlaLoggera() {
    mkdir("logs", 0700);
    
    // Pobranie aktualnego czasu
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
        exit(1);
    }

    struct KomunikatLog msg;
    while (1) {
        if (msgrcv(id_kolejki, &msg, sizeof(msg) - sizeof(long), 0, 0) == -1) {
            perror("Blad msgrcv");
            break;
        }

        // Sprawdzenie czy to sygnal konca
        if (msg.typ_komunikatu == TYP_KONIEC) {
            break; 
        }

        // Logika formatowania
        const char* prefix = "";
        const char* kolor = KOLOR_RESET;
        
        switch (msg.typ_logu) {
            case LOG_INFO:
                prefix = "[INFO] ";
                kolor = KOLOR_ZIELONY;
                break;
            case LOG_OSTRZEZENIE: prefix = "[OSTRZ] ";
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

        // Czas
        teraz = time(NULL);
        czas = localtime(&teraz);

        char czas_buf[16];
        sprintf(czas_buf, "[%02d:%02d:%02d] ", czas->tm_hour, czas->tm_min, czas->tm_sec);

        // Wyjscie na ekran z kolorami
        printf("%s%s%s%s%s\n", czas_buf, kolor, prefix, msg.tresc, KOLOR_RESET);

        // Zapis do pliku
        char bufor_pliku[512];
        sprintf(bufor_pliku, "%s%s%s\n", czas_buf, prefix, msg.tresc);
        write(deskryptor_pliku, bufor_pliku, strlen(bufor_pliku));
    }

    close(deskryptor_pliku);
    exit(0);
}

void InicjalizujSystemLogowania() {
    
    // Inicjalizacja IPC
    id_kolejki = msgget(ID_KOLEJKI, IPC_CREAT | 0600);
    if (id_kolejki == -1) {
        perror("Blad tworzenia kolejki komunikatow");
        exit(1);
    }
}

void UruchomProcesLogujacy() {
    pid_loggera = fork();
    if (pid_loggera == 0) {
        PetlaLoggera(); // Proces potomny
    }
}

void ZamknijSystemLogowania() {
    // Wyslanie komunikatu o zakonczeniu
    struct KomunikatLog msg;
    msg.typ_komunikatu = TYP_KONIEC;
    msgsnd(id_kolejki, &msg, sizeof(msg) - sizeof(long), 0);

    // Czekanie na zakonczenie procesu loggera
    if (pid_loggera != -1) {
        waitpid(pid_loggera, NULL, 0);
    }

    // Usuniecie kolejki
    msgctl(id_kolejki, IPC_RMID, NULL);
}

void ZapiszLog(int typ_logu, const char* format) {
    if (id_kolejki == -1) return;

    struct KomunikatLog msg;
    msg.typ_komunikatu = 1; // Zwykly komunikat
    msg.typ_logu = typ_logu;
    strncpy(msg.tresc, format, 255);
    msg.tresc[255] = '\0';

    // Wyslanie do kolejki
    if (msgsnd(id_kolejki, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
        perror("Blad msgsnd");
    }
}
