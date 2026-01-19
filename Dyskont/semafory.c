#include "semafory.h"
#include "pamiec_wspoldzielona.h"
#include "wspolne.h"



//Operacja na semaforze
static int OperacjaSemafor(int sem_id, int sem_num, int wartosc, StanSklepu* stan, const char* blad_msg) {
    struct sembuf operacja = { sem_num, wartosc, SEM_UNDO };
    
    while (1) {
        if (stan && CZY_KONCZYC(stan)) return -2; //Sprawdzenie czy semafor ma zostac usuniety
        
        if (semop(sem_id, &operacja, 1) == 0) return 0;
        
        //Przerwanie semafora sygnalem
        if (errno == EINTR) continue;
        
        //Inny blad
        if (errno != EINVAL && errno != EIDRM && errno != EINTR) perror(blad_msg);
        return -1;
    }
}

//Inicjalizacja zestawu semaforow
int InicjalizujSemafory() {
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
    
    //Semafory sygnalizacyjne do otwierania kas stacjonarnych
    wartosci[SEM_OTWORZ_KASA_STACJONARNA_1] = 0;
    wartosci[SEM_OTWORZ_KASA_STACJONARNA_2] = 0;
    
    //Semafory dla kas samoobslugowych
    for (int i = 0; i < LICZBA_KAS_SAMOOBSLUGOWYCH; i++) {
        wartosci[SEM_KASA_SAMOOBSLUGOWA_0 + i] = 0;
    }
    
    //Semafor wpuszczajacy klientow
    wartosci[SEM_WEJSCIE_DO_SKLEPU] = 0;
    
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
int ZajmijSemafor(int sem_id, int sem_num, StanSklepu* stan) {
    return OperacjaSemafor(sem_id, sem_num, -1, stan, "Blad zajmowania semafora");
}

//Zwalnia semafor
int ZwolnijSemafor(int sem_id, int sem_num) {
    return OperacjaSemafor(sem_id, sem_num, 1, NULL, "Blad zwalniania semafora");
}

//Usuniecie zestawu semaforow
int UsunSemafory(int sem_id) {
    if (semctl(sem_id, 0, IPC_RMID) == -1) {
        perror("Blad usuniecia zestawu semaforow");
        return -1;
    }
    return 0;
}
