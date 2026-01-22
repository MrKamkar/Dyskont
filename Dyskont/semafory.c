#include "semafory.h"
#include "pamiec_wspoldzielona.h"
#include "pamiec_wspoldzielona.h"

//Operacja na semaforze
static int OperacjaSemafor(int sem_id, int sem_num, int wartosc, const char* blad_msg) {
    struct sembuf operacja = { sem_num, wartosc, SEM_UNDO };
    
    while (1) {
        if (semop(sem_id, &operacja, 1) == 0) return 0;
        
        //Przerwanie semafora sygnalem
        if (errno == EINTR) return -1;
        
        //Inny blad
        if (errno != EINVAL && errno != EIDRM) perror(blad_msg);
        return -1;
    }
}

//Inicjalizacja zestawu semaforow
int InicjalizujSemafory(int max_klientow) {
    key_t klucz = GenerujKluczIPC(ID_IPC_SEMAFORY);
    if (klucz == -1) return -1;

    int sem_id = semget(klucz, SEM_LICZBA, IPC_CREAT | IPC_EXCL | 0600);
    if (sem_id == -1) {
        perror("Blad utworzenia zestawu semaforow");
        return -1;
    }

    //Inicjalizacja semaforow
    union semun arg;
    unsigned short wartosci[SEM_LICZBA];
    
    //Mutexy binarne => 1 oznacza wolny do uzytku
    wartosci[MUTEX_PAMIEC_WSPOLDZIELONA] = 1;
    wartosci[MUTEX_KOLEJKI_VIP] = 1; 

    
    //Semafory sygnalizacyjne do otwierania kas stacjonarnych
    wartosci[SEM_OTWORZ_KASA_STACJONARNA_1] = 0;
    wartosci[SEM_OTWORZ_KASA_STACJONARNA_2] = 0;
    
    //Semafor nowego klienta
    wartosci[SEM_NOWY_KLIENT] = 0;
    
    //Semafory kolejek komunikatow - inicjalnie 0 (blokada az do zwolnienia miejsca)
    wartosci[SEM_KOLEJKA_KASA_1] = 0;
    wartosci[SEM_KOLEJKA_KASA_2] = 0;
    wartosci[SEM_KOLEJKA_WSPOLNA] = 0;
    wartosci[SEM_KOLEJKA_SAMO] = 0;
    wartosci[SEM_KOLEJKA_PRACOWNIK] = 0;
    
    //Semafor wpuszczajacy klientow (Max klientow w sklepie)
    if (max_klientow > 0) wartosci[SEM_WEJSCIE_DO_SKLEPU] = (unsigned short)max_klientow;
    else wartosci[SEM_WEJSCIE_DO_SKLEPU] = MAX_KLIENTOW_ROWNOCZESNIE_DOMYSLNIE;
    
    arg.array = wartosci;
    
    if (semctl(sem_id, 0, SETALL, arg) == -1) {
        perror("Blad inicjalizacji semaforow");
        semctl(sem_id, 0, IPC_RMID);
        return -1;
    }

    return sem_id;
}

//Dolaczenie do istniejacych semaforow
int DolaczSemafory() {
    key_t klucz = GenerujKluczIPC(ID_IPC_SEMAFORY);
    if (klucz == -1) return -1;

    int sem_id = semget(klucz, SEM_LICZBA, 0600);
    if (sem_id == -1) {
        perror("Blad uzyskania ID zestawu semaforow");
        return -1;
    }

    return sem_id;
}

//Zajmuje semafor
int ZajmijSemafor(int sem_id, int sem_num) {
    return OperacjaSemafor(sem_id, sem_num, -1, "Blad zajmowania semafora");
}

//Zwalnia semafor
int ZwolnijSemafor(int sem_id, int sem_num) {
    return OperacjaSemafor(sem_id, sem_num, 1, "Blad zwalniania semafora");
}

//Pobiera liczbe klientow w sklepie na podstawie wartosci semafora
int PobierzLiczbeKlientow(int sem_id, int max_klientow) {
    if (sem_id == -1) return 0;
    
    //Pobierz aktualna wartosc semafora (wolne miejsca)
    int wolne_miejsca = semctl(sem_id, SEM_WEJSCIE_DO_SKLEPU, GETVAL);
    if (wolne_miejsca == -1) {
        perror("Blad semctl GETVAL (PobierzLiczbeKlientow)");
        return 0;
    }
    
    int klienci = max_klientow - wolne_miejsca;
    if (klienci < 0) klienci = 0;
    if (klienci > max_klientow) klienci = max_klientow; //Zabezpieczenie
    
    return klienci;
}

//Usuniecie zestawu semaforow
int UsunSemafory(int sem_id) {
    if (semctl(sem_id, 0, IPC_RMID) == -1) {
        perror("Blad usuniecia zestawu semaforow");
        return -1;
    }
    return 0;
}
