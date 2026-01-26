#include "semafory.h"
#include "pamiec_wspoldzielona.h"
#include "pamiec_wspoldzielona.h"
#include <fcntl.h>
#include <unistd.h>

//Pobiera systemowy limit wielkosci kolejki (msgmnb) z /proc
size_t PobierzLimitKolejki(void)
{
    char buf[32];
    size_t limit = 16384;

    int fd = open("/proc/sys/kernel/msgmnb", O_RDONLY);
    if (fd < 0)
        return limit;

    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);

    if (n > 0) {
        buf[n] = '\0';
        limit = strtoul(buf, NULL, 10);
    }

    return limit;
}


//Wykonuje operacje na semaforach
static int OperacjaSemafor(int sem_id, int sem_num, int wartosc, const char* blad_msg) {
    short flagi = 0;
    //Uzywamy SEM_UNDO tylko dla Mutexow oraz semaforow "symetrycznych" (P i V w tym samym procesie)
    //Dzieki temu, jesli proces klienta zginie, zasoby zostana zwolnione
    if (sem_num == MUTEX_PAMIEC_WSPOLDZIELONA || 
        sem_num == MUTEX_KOLEJKI_VIP ||
        sem_num == SEM_WEJSCIE_DO_SKLEPU ||
        sem_num == SEM_KOLEJKA_SAMO) {
        flagi = SEM_UNDO;
    }
    
    struct sembuf operacja = { sem_num, wartosc, flagi };
    
    while (1) {
        if (semop(sem_id, &operacja, 1) == 0) return 0;
        
        //Przerwanie semafora sygnalem - kontynuujemy
        if (errno == EINTR) continue;
        
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

    //Pobieramy systemowy limit msgmnb
    size_t limit_systemowy = PobierzLimitKolejki();
    
    //Obliczamy pojemnosc kolejek dla roznych typow komunikatow
    int poj_samo = (limit_systemowy / sizeof(MsgKasaSamo)) - 1;
    int poj_stacj = (limit_systemowy / sizeof(MsgKasaStacj)) - 1;
    int poj_prac = (limit_systemowy / sizeof(MsgPracownik)) - 1;
    
    if (poj_samo < 1) poj_samo = 1;
    if (poj_stacj < 1) poj_stacj = 1;
    if (poj_prac < 1) poj_prac = 1;
    
    printf("Limity kolejek z jednym zapasowym miejscem:\n");
    printf(" - Samoobslugowa (size: %zu): %d komunikatow\n", sizeof(MsgKasaSamo), poj_samo);
    printf(" - Stacjonarne   (size: %zu): %d komunikatow\n", sizeof(MsgKasaStacj), poj_stacj);
    printf(" - Pracownik     (size: %zu): %d komunikatow\n", sizeof(MsgPracownik), poj_prac);

    //Semafory kolejek komunikatow
    wartosci[SEM_KOLEJKA_KASA_1] = poj_stacj;
    wartosci[SEM_KOLEJKA_KASA_2] = poj_stacj;
    wartosci[SEM_KOLEJKA_WSPOLNA] = poj_stacj;
    wartosci[SEM_KOLEJKA_SAMO] = poj_samo;
    wartosci[SEM_KOLEJKA_PRACOWNIK] = poj_prac;
    
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
