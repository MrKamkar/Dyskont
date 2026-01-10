#include "semafory.h"
#include <errno.h>

//Inicjalizacja zestawu semaforow
int InicjalizujSemafory(const char* sciezka) {
    //Generowanie klucza dla semaforow (uzywamy 'M' dla semaforow)
    key_t klucz = ftok(sciezka, 'M');
    if (klucz == -1) {
        perror("Blad generowania klucza dla semaforow");
        return -1;
    }

    //Utworzenie zestawu semaforow (SEM_LICZBA semaforow)
    int sem_id = semget(klucz, SEM_LICZBA, IPC_CREAT | IPC_EXCL | 0600);
    if (sem_id == -1) {
        perror("Blad utworzenia zestawu semaforow");
        return -1;
    }

    //Inicjalizacja wartosci semaforow
    union semun arg;
    
    //Semafor pamieci wspoldzielonej (mutex binarny, wartosc poczatkowa = 1)
    arg.val = 1;
    if (semctl(sem_id, SEM_PAMIEC_WSPOLDZIELONA, SETVAL, arg) == -1) {
        perror("Blad inicjalizacji semafora pamieci wspoldzielonej");
        semctl(sem_id, 0, IPC_RMID); //Czyszczenie w przypadku błędu
        return -1;
    }

    //Semafor kolejki samoobslugowej (mutex binarny, wartosc poczatkowa = 1)
    arg.val = 1;
    if (semctl(sem_id, SEM_KOLEJKA_SAMO, SETVAL, arg) == -1) {
        perror("Blad inicjalizacji semafora kolejki");
        semctl(sem_id, 0, IPC_RMID); //Czyszczenie w przypadku błędu
        return -1;
    }

    //Semafor kasy stacjonarnej 1 (mutex binarny, wartosc poczatkowa = 1)
    arg.val = 1;
    if (semctl(sem_id, SEM_KASA_STACJONARNA_1, SETVAL, arg) == -1) {
        perror("Blad inicjalizacji semafora kasy stacjonarnej 1");
        semctl(sem_id, 0, IPC_RMID);
        return -1;
    }

    //Semafor kasy stacjonarnej 2 (mutex binarny, wartosc poczatkowa = 1)
    arg.val = 1;
    if (semctl(sem_id, SEM_KASA_STACJONARNA_2, SETVAL, arg) == -1) {
        perror("Blad inicjalizacji semafora kasy stacjonarnej 2");
        semctl(sem_id, 0, IPC_RMID);
        return -1;
    }

    return sem_id;
}

//Dolaczenie do istniejacych semaforow
int DolaczSemafory(const char* sciezka) {
    //Generowanie tego samego klucza
    key_t klucz = ftok(sciezka, 'M');
    if (klucz == -1) {
        perror("Blad generowania klucza dla semaforow");
        return -1;
    }

    //Pobranie ID istniejacego zestawu
    int sem_id = semget(klucz, SEM_LICZBA, 0600);
    if (sem_id == -1) {
        perror("Blad uzyskania ID zestawu semaforow");
        return -1;
    }

    return sem_id;
}

//Zajmuje semafor (zmniejsza o 1)
int ZajmijSemafor(int sem_id, int sem_num) {
    struct sembuf operacja;
    operacja.sem_num = sem_num;    //Numer semafora
    operacja.sem_op = -1;          //Zmniejsz wartość o 1
    operacja.sem_flg = 0;          //Blokujace oczekiwanie

    if (semop(sem_id, &operacja, 1) == -1) {
        //Nie wyswietli bledu gdy semafor zostal usuniety
        if (errno != EINVAL && errno != EIDRM) {
            perror("Blad zajmowania semafora");
        }
        return -1;
    }

    return 0;
}

//Zwalnia semafor (zwieksza o 1)
int ZwolnijSemafor(int sem_id, int sem_num) {
    struct sembuf operacja;
    operacja.sem_num = sem_num;    //Numer semafora
    operacja.sem_op = 1;           //Zwieksz wartosc o 1
    operacja.sem_flg = 0;          //Bez flag

    if (semop(sem_id, &operacja, 1) == -1) {
        //Nie wyswietli bledu gdy semafor zostal usuniety
        if (errno != EINVAL && errno != EIDRM) {
            perror("Blad zwalniania semafora");
        }
        return -1;
    }

    return 0;
}

//Usuniecie zestawu semaforow
int UsunSemafory(int sem_id) {
    if (semctl(sem_id, 0, IPC_RMID) == -1) {
        perror("Blad usuniecia zestawu semaforow");
        return -1;
    }

    return 0;
}
