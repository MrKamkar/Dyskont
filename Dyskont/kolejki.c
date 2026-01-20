#include "kolejki.h"
#include "logi.h"
#include "semafory.h"
#include <stdio.h>
#include <errno.h>

//Pobiera ID istniejacej kolejki
int PobierzIdKolejki(char id_projektu) {
    key_t klucz = GenerujKluczIPC(id_projektu);
    if (klucz == -1) return -1;
    
    int msg_id = msgget(klucz, 0600);
    if (msg_id == -1) {
        //Tylko loguj blad jesli to nie jest zwykle sprawdzenie czy kolejka istnieje
        //Ale tutaj zakladamy ze ma istniec
        //printf("Blad msgget dla id %c: %d\n", id_projektu, errno);
    }
    return msg_id;
}

//Tworzy nowa kolejke
int StworzKolejke(char id_projektu) {
    key_t klucz = GenerujKluczIPC(id_projektu);
    if (klucz == -1) return -1;
    
    int msg_id = msgget(klucz, IPC_CREAT | 0600);
    if (msg_id == -1) {
        perror("StworzKolejke msgget");
        return -1;
    }
    return msg_id;
}

//Usuwa kolejke
void UsunKolejke(int msg_id) {
    if (msg_id != -1) {
        if (msgctl(msg_id, IPC_RMID, NULL) == -1) {
            //Ignoruj błędy jeśli kolejka nie istnieje
            if (errno != EINVAL && errno != EIDRM) {
                perror("UsunKolejke msgctl");
            }
        }
    }
}

//Wysyla komunikat
int WyslijKomunikat(int msg_id, void* msg, size_t size) {
    if (msg_id == -1 || !msg) return -1;
    
    if (msgsnd(msg_id, msg, size, 0) == -1) {
        return -1;
    }
    return 0;
}

//Odbiera komunikat
int OdbierzKomunikat(int msg_id, void* msg, size_t size, long typ, int flagi) {
    if (msg_id == -1 || !msg) return -1;
    
    if (msgrcv(msg_id, msg, size, typ, flagi) == -1) {
        return -1;
    }
    return 0;
}

//Pobiera rozmiar kolejki (bezposrednio z kernela)
int PobierzRozmiarKolejki(int msg_id) {
    if (msg_id == -1) return -1;
    struct msqid_ds buf;
    if (msgctl(msg_id, IPC_STAT, &buf) == -1) {
        return -1;
    }
    return (int)buf.msg_qnum;
}

//Zmniejsza limit kolejki o rozmiar jednego komunikatu, aby wymusic blokowanie
int ZostawMiejsceWKolejce(int msg_id, size_t msg_size) {
    if (msg_id == -1) return -1;
    
    struct msqid_ds buf;
    
    //Pobieramy aktualne ustawienia
    if (msgctl(msg_id, IPC_STAT, &buf) == -1) {
        perror("ZostawMiejsceWKolejce msgctl STAT");
        return -1;
    }
    
    //Zmniejszamy limit o rozmiar jednej wiadomosci
    if (buf.msg_qbytes >= msg_size) {
        buf.msg_qbytes -= msg_size;
    } else {
        buf.msg_qbytes = 0;
    }
    
    //Ustawiamy nowy limit
    if (msgctl(msg_id, IPC_SET, &buf) == -1) {
        perror("ZostawMiejsceWKolejce msgctl SET");
        return -1;
    }
    
    ZapiszLogF(LOG_INFO, "Zmieniono limit kolejki [ID: %d] na %lu bajtow, by zostawić miejsce na 1 komunikat.", msg_id, (unsigned long)buf.msg_qbytes);
               
    return 0;
}

//Wysyla komunikat z tymczasowym podniesieniem limitu (dla odpowiedzi)
int WyslijKomunikatVIP(int sem_id, int msg_id, void* msg, size_t size) {
    if (msg_id == -1 || !msg) return -1;
    
    //Synchronizujemy operacje zmiany limitu, aby uniknac wyscigu miedzy kasjerami
    ZajmijSemafor(sem_id, MUTEX_KOLEJKI_VIP);

    struct msqid_ds buf;
    //Pobieramy aktualny limit
    if (msgctl(msg_id, IPC_STAT, &buf) == -1) {
        perror("WyslijKomunikatVIP msgctl STAT");
        ZwolnijSemafor(sem_id, MUTEX_KOLEJKI_VIP);
        return -1;
    }
    
    unsigned long stary_limit = buf.msg_qbytes;
    
    //Tymczasowo podnosimy limit o rozmiar wysylanej wiadomosci
    buf.msg_qbytes += size;
    if (msgctl(msg_id, IPC_SET, &buf) == -1) {
        perror("WyslijKomunikatVIP msgctl SET (podnoszenie)");
    }
    
    //Wysylamy (powinno przejsc do "zarezerwowanego" miejsca)
    int res = WyslijKomunikat(msg_id, msg, size);
    
    //Przywracamy poprzedni limit dla innych procesow
    buf.msg_qbytes = stary_limit;
    if (msgctl(msg_id, IPC_SET, &buf) == -1) {
        perror("WyslijKomunikatVIP msgctl SET (przywracanie)");
    }
    
    //Zwalniamy mutex
    ZwolnijSemafor(sem_id, MUTEX_KOLEJKI_VIP);

    return res;
}



