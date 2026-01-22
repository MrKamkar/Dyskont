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
        printf("Blad msgget dla id %c: %d\n", id_projektu, errno);
        return -1;
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
int WyslijKomunikat(int msg_id, void* msg, size_t size, int sem_id, int sem_num) {
    if (msg_id == -1 || !msg) return -1;
    
    //Wersja ze "strazakiem" - semafor liczy wolne miejsca w kolejce
    if (sem_id != -1 && sem_num != -1) {
        if (ZajmijSemafor(sem_id, sem_num) == -1) return -1;
    }

    if (msgsnd(msg_id, msg, size, 0) == -1) {
        //W razie bledu wyslania (np. kolejka usunieta), oddajemy semafor
        if (sem_id != -1 && sem_num != -1) {
            ZwolnijSemafor(sem_id, sem_num);
        }
        return -1;
    }

    return 0;
}

//Odbiera komunikat
int OdbierzKomunikat(int msg_id, void* msg, size_t size, long typ, int flagi, int sem_id, int sem_num) {
    if (msg_id == -1 || !msg) return -1;
    
    if (msgrcv(msg_id, msg, size, typ, flagi) == -1) {
        return -1;
    }

    //Zwalniamy miejsce w kolejce (strazak)
    if (sem_id != -1 && sem_num != -1) {
        ZwolnijSemafor(sem_id, sem_num);
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

//Wysyla komunikat z normalnym limitem kolejki
int WyslijKomunikatVIP(int msg_id, void* msg, size_t size) {
    if (msg_id == -1 || !msg) return -1;
    int res = WyslijKomunikat(msg_id, msg, size, -1, -1);
    return res;
}