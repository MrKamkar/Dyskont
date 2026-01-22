#ifndef KOLEJKI_H
#define KOLEJKI_H

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <stddef.h>
#include "pamiec_wspoldzielona.h"

//Identyfikatory projektow dla kolejek komunikatow
#define ID_IPC_KASA_1 '1'
#define ID_IPC_KASA_2 '2'
#define ID_IPC_KASA_WSPOLNA 'W'
#define ID_IPC_SAMO '3'
#define ID_IPC_PRACOWNIK '4'

//Pobiera ID istniejacej kolejki (uzywa ftok + msgget)
int PobierzIdKolejki(char id_projektu);

//Tworzy nowa kolejke lub zwraca istniejaca (uzywa ftok + msgget z IPC_CREAT)
int StworzKolejke(char id_projektu);

//Usuwa kolejke o podanym ID
void UsunKolejke(int msg_id);

//Wysyla komunikat do kolejki z blokowaniem na semaforze jesli pelna
int WyslijKomunikat(int msg_id, void* msg, size_t size, int sem_id, int sem_num);

//Zwraca 0 w przypadku sukcesu, -1 w przypadku bledu
int OdbierzKomunikat(int msg_id, void* msg, size_t size, long typ, int flagi, int sem_id, int sem_num);

//Odbiera komunikat bez sygnalizacji semafora
int OdbierzKomunikatVIP(int msg_id, void* msg, size_t size, long typ, int flagi);

//Zwraca liczbe komunikatow lub -1 w przypadku bledu
int PobierzRozmiarKolejki(int msg_id);

//Wysylij komunikat bez blokowania semafora
int WyslijKomunikatVIP(int msg_id, void* msg, size_t size);

#endif
