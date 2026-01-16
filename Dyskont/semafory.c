#include "semafory.h"
#include "pamiec_wspoldzielona.h"

//Globalny wskaznik do pamieci wspoldzielonej dla sprawdzania ewakuacji
static StanSklepu* g_stan_ewakuacji = NULL;

//Ustawia wskaznik pamieci wspoldzielonej dla sprawdzania ewakuacji
void UstawPamiecDlaSemaforow(StanSklepu* stan) {
    g_stan_ewakuacji = stan;
}

//Generowanie klucza dla semaforow
static key_t GenerujKlucz() {
    key_t klucz = ftok(IPC_SCIEZKA, 'M');
    if (klucz == -1) {
        perror("Blad generowania klucza dla semaforow");
    }
    return klucz;
}

//Operacja na semaforze - blokujaca z mozliwoscia przerwania przy ewakuacji
static int OperacjaSemafor(int sem_id, int sem_num, int wartosc, const char* blad_msg) {
    struct sembuf operacja = { sem_num, wartosc, 0 };
    struct timespec timeout = {1, 0};  //1s timeout dla sprawdzania ewakuacji
    
    while (1) {
        //Sprawdz flage ewakuacji z pamieci wspoldzielonej
        if (g_stan_ewakuacji && g_stan_ewakuacji->flaga_ewakuacji) {
            return -2;
        }
        
        if (semtimedop(sem_id, &operacja, 1, &timeout) == 0) {
            return 0;  //Sukces
        }
        
        //EINTR (sygnal) lub EAGAIN (timeout) - powtorz
        if (errno == EINTR || errno == EAGAIN) {
            continue;
        }
        
        //Inny blad
        if (errno != EINVAL && errno != EIDRM) {
            perror(blad_msg);
        }
        return -1;
    }
}

//Inicjalizacja zestawu semaforow
int InicjalizujSemafory() {
    key_t klucz = GenerujKlucz();
    if (klucz == -1) return -1;

    int sem_id = semget(klucz, SEM_LICZBA, IPC_CREAT | IPC_EXCL | 0600);
    if (sem_id == -1) {
        perror("Blad utworzenia zestawu semaforow");
        return -1;
    }

    //Inicjalizacja semaforow
    union semun arg;
    unsigned short wartosci[SEM_LICZBA];
    
    //Mutexy binarne (wartosc 1 = wolny)
    wartosci[MUTEX_PAMIEC_WSPOLDZIELONA] = 1;
    wartosci[MUTEX_KOLEJKA_SAMO] = 1;
    wartosci[MUTEX_KASA_STACJONARNA_1] = 1;
    wartosci[MUTEX_KASA_STACJONARNA_2] = 1;
    
    //Semafory zliczajace
    wartosci[SEM_WOLNE_KASY_SAMO] = 3;     //3 kasy otwarte na start (MIN_KAS_SAMO_CZYNNYCH)
    wartosci[SEM_KLIENCI_KOLEJKA_1] = 0;   //Brak klientow na start
    wartosci[SEM_KLIENCI_KOLEJKA_2] = 0;   //Brak klientow na start
    
    //Semafory sygnalizacyjne (wartosc 0 = brak sygnalu)
    wartosci[SEM_ODBLOKUJ_KASA_SAMO_0] = 0;
    wartosci[SEM_ODBLOKUJ_KASA_SAMO_1] = 0;
    wartosci[SEM_ODBLOKUJ_KASA_SAMO_2] = 0;
    wartosci[SEM_ODBLOKUJ_KASA_SAMO_3] = 0;
    wartosci[SEM_ODBLOKUJ_KASA_SAMO_4] = 0;
    wartosci[SEM_ODBLOKUJ_KASA_SAMO_5] = 0;
    wartosci[SEM_OTWORZ_KASA_STACJ_1] = 0;
    wartosci[SEM_OTWORZ_KASA_STACJ_2] = 0;
    wartosci[SEM_CZEKAJ_SYGNAL] = 0;
    
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
    key_t klucz = GenerujKlucz();
    if (klucz == -1) return -1;

    int sem_id = semget(klucz, SEM_LICZBA, 0600);
    if (sem_id == -1) {
        perror("Blad uzyskania ID zestawu semaforow");
        return -1;
    }

    return sem_id;
}

//Zajmuje semafor (zmniejsza o 1)
int ZajmijSemafor(int sem_id, int sem_num) {
    return OperacjaSemafor(sem_id, sem_num, -1, "Blad zajmowania semafora");
}

//Zwalnia semafor (zwieksza o 1)
int ZwolnijSemafor(int sem_id, int sem_num) {
    return OperacjaSemafor(sem_id, sem_num, 1, "Blad zwalniania semafora");
}

//Usuniecie zestawu semaforow
int UsunSemafory(int sem_id) {
    if (semctl(sem_id, 0, IPC_RMID) == -1) {
        perror("Blad usuniecia zestawu semaforow");
        return -1;
    }
    return 0;
}
