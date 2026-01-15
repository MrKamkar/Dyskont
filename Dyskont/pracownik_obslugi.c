#include "pracownik_obslugi.h"
#include "wspolne.h"
#include <string.h>
#include <sys/select.h>
#include <signal.h>

//Inicjalizacja FIFO, tworzy lacze nazwane
int InicjalizujFifoObslugi() {
    //Usun istniejace FIFO jesli jest
    unlink(FIFO_OBSLUGA);
    
    //Utworz nowe FIFO z prawami odczytu/zapisu
    if (mkfifo(FIFO_OBSLUGA, 0666) == -1) {
        if (errno != EEXIST) {
            perror("Blad tworzenia FIFO obslugi");
            return -1;
        }
    }
    
    return 0;
}

//Usuniecie FIFO
void UsunFifoObslugi() {
    if (unlink(FIFO_OBSLUGA) == -1) {
        perror("Blad usuwania FIFO obslugi");
    }
}

//Wysylanie zadania do pracownika przez FIFO
int WyslijZadanieObslugi(ZadanieObslugi* zadanie) {
    if (!zadanie) return -1;
    
    //Otwarcie FIFO do zapisu (nieblokujace)
    int fd = open(FIFO_OBSLUGA, O_WRONLY | O_NONBLOCK);
    if (fd == -1) {
        //ENXIO = brak czytnika, ENOENT = brak pliku
        if (errno != ENXIO && errno != ENOENT) {
            perror("Blad otwarcia FIFO do zapisu");
        }
        return -1;
    }
    
    //Zapis struktury zadania
    ssize_t napisano = write(fd, zadanie, sizeof(ZadanieObslugi));
    if (napisano == -1) {
        perror("Blad zapisu do FIFO");
        close(fd);
        return -1;
    }
    
    close(fd);
    return 0;
}

//Odbieranie zadania przez pracownika
int OdbierzZadanieObslugi(ZadanieObslugi* zadanie) {
    if (!zadanie) return -1;
    
    //Otwarcie FIFO do odczytu (blokujace)
    int fd = open(FIFO_OBSLUGA, O_RDONLY);
    if (fd == -1) {
        perror("Blad otwarcia FIFO do odczytu");
        return -1;
    }
    
    //Odczyt struktury zadania
    ssize_t przeczytano = read(fd, zadanie, sizeof(ZadanieObslugi));
    if (przeczytano <= 0) {
        close(fd);
        return -1;
    }
    
    close(fd);
    return 0;
}

//Glowna funkcja procesu pracownika obslugi
#ifdef PRACOWNIK_STANDALONE
int main() {
    StanSklepu* stan_sklepu;
    int sem_id;
    
    if (InicjalizujProcesPochodny(&stan_sklepu, &sem_id, "Pracownik") == -1) {
        return 1;
    }
    
    signal(SIGTERM, ObslugaSygnaluWyjscia);
    
    ZapiszLog(LOG_INFO, "Pracownik obslugi: Proces uruchomiony, nasluchuje na FIFO...");
    
    
    //Otwarcie FIFO do odczytu nieblokujace, pozwala sprawdzac flage ewakuacji
    int fd = open(FIFO_OBSLUGA, O_RDONLY | O_NONBLOCK);
    if (fd == -1) {
        perror("Pracownik: Blad otwarcia FIFO");
        OdlaczPamiecWspoldzielona(stan_sklepu);
        return 1;
    }
    
    //Glowna petla pracownika
    while (1) {
        //Reaguj TYLKO na SIGTERM, nie na flaga_ewakuacji
        if (g_sygnal_wyjscia) {
            ZapiszLog(LOG_INFO, "Pracownik obslugi: Otrzymano SIGTERM - koncze prace.");
            break;
        }
        
        //Blokujace czekanie na dane z FIFO (max 1 sek)
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);
        struct timeval timeout = {1, 0};  //1 sekunda
        
        int gotowe = select(fd + 1, &readfds, NULL, NULL, &timeout);
        
        if (gotowe > 0 && FD_ISSET(fd, &readfds)) {
            ZadanieObslugi zadanie;
            ssize_t przeczytano = read(fd, &zadanie, sizeof(ZadanieObslugi));
            
            if (przeczytano == sizeof(ZadanieObslugi)) {
                //Przetworzenie zadania
                switch (zadanie.typ) {
                    case ZADANIE_ODBLOKUJ_KASE:
                        ZapiszLogF(LOG_INFO, "Pracownik obslugi: Odblokowuje kase samoobslugowa [%d] dla klienta [%d]",
                                zadanie.id_kasy + 1, zadanie.id_klienta);
                        
                        //Symulacja czasu interwencji
                        SYMULACJA_USLEEP(stan_sklepu, 500000); //500ms
                        
                        //Odblokowanie kasy w pamieci wspoldzielonej
                        ZajmijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
                        if (stan_sklepu->kasy_samo[zadanie.id_kasy].stan == KASA_ZABLOKOWANA) {
                            stan_sklepu->kasy_samo[zadanie.id_kasy].stan = KASA_ZAJETA;
                        }
                        ZwolnijSemafor(sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
                        
                        //Sygnal odblokowania - budzi czekajacego klienta
                        ZwolnijSemafor(sem_id, SEM_ODBLOKUJ_KASA_SAMO(zadanie.id_kasy));
                        
                        ZapiszLogF(LOG_INFO, "Pracownik obslugi: Kasa samoobslugowa [%d] odblokowana.", zadanie.id_kasy + 1);
                        break;
                        
                    case ZADANIE_WERYFIKUJ_WIEK:
                        ZapiszLogF(LOG_INFO, "Pracownik obslugi: Weryfikacja wieku klienta [%d], wiek: %d",
                                zadanie.id_klienta, zadanie.wiek_klienta);
                        
                        SYMULACJA_USLEEP(stan_sklepu, 300000);
                        
                        if (zadanie.wiek_klienta >= 18) {
                            ZapiszLogF(LOG_INFO, "Pracownik obslugi: Wiek OK - klient [%d] moze kupic alkohol.",
                                    zadanie.id_klienta);
                        } else {
                            ZapiszLogF(LOG_BLAD, "Pracownik obslugi: ODMOWA - klient [%d] niepelnoletni!",
                                    zadanie.id_klienta);
                        }
                        break;
                }
            } else if (przeczytano == 0) {
                //Koniec danych, ponowne otwarcie FIFO
                close(fd);
                fd = open(FIFO_OBSLUGA, O_RDONLY | O_NONBLOCK);
                if (fd == -1) break;
            }
        }
        //Jesli timeout (gotowe == 0) - kontynuuj petle, sprawdzi ewakuacje
    }
    
    close(fd);
    OdlaczPamiecWspoldzielona(stan_sklepu);
    
    return 0;
}
#endif
