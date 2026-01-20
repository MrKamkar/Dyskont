#include "klient.h"
#include "wspolne.h"
#include "kasjer.h"
#include "kasa_samoobslugowa.h"
#include "kolejki.h"
#include <sys/wait.h>

#ifdef KLIENT_STANDALONE

//Zmienne globalne dla obslugi sygnalow
static StanSklepu* g_stan_sklepu = NULL;
static int g_sem_id = -1;
static int g_is_parent = 1; //Domyslnie jestesmy rodzicem (Generatorem)
static volatile sig_atomic_t g_alarm_timeout = 0;

//Stworzenie klienta
Klient* StworzKlienta(int id) {
    Klient* k = (Klient*)malloc(sizeof(Klient));
    if (!k) return NULL;

    k->id = id;
    k->wiek = 6 + (rand() % 85); //od 6 do 90 lat
    
    //Losuje od razu ile kupi (od 3 do 10 produktow)
    k->ilosc_planowana = 3 + (rand() % 8);
    k->koszyk = (Produkt*)malloc(sizeof(Produkt) * k->ilosc_planowana);
    
    //Obsluga bledu alokacji
    if (!k->koszyk) {
        free(k);
        return NULL;
    }

    //Na start koszyk jest pusty, ale ma zarezerwowana pamiec
    k->liczba_produktow = 0;
    k->czas_dolaczenia_do_kolejki = 0;

    return k;
}

//Usuwanie klienta
void UsunKlienta(Klient* k) {
    if (k) {
        if (k->koszyk) {
            free(k->koszyk);
        }
        free(k);
    }
}

//Sprawdzenie czy kategoria istnieje w koszyku
static int CzyMaKategorie(const Klient* k, KategoriaProduktu kat) {
    if (!k) return 0;
    for (unsigned int i = 0; i < k->liczba_produktow; i++) {
        if (k->koszyk[i].kategoria == kat) return 1;
    }
    return 0;
}

//Zrobienie zakupow
void ZrobZakupy(Klient* k, const StanSklepu* stan_sklepu) {
    if (!k || !k->koszyk || !stan_sklepu) return;

    //Wypelnia koszyk do zaplanowanej ilosci produktow
    while (k->liczba_produktow < k->ilosc_planowana) {

        //Symulacja chodzenia po sklepie i wyboru produktu (od 3 do 15 sekund)
        SYMULACJA_USLEEP(stan_sklepu, (3000000 + (rand() % 12000001)));

        //Wybor produktu
        int indeks = 0;
        for (int proby = 0; proby < 20; proby++) {
            indeks = rand() % stan_sklepu->liczba_produktow;
            if (!CzyMaKategorie(k, stan_sklepu->magazyn[indeks].kategoria)) {
                break;
            }
        }
        
        k->koszyk[k->liczba_produktow++] = stan_sklepu->magazyn[indeks];
    }
}

int CzyZawieraAlkohol(const Klient* k) {
    return CzyMaKategorie(k, KAT_ALKOHOL);
}

//Obliczenie sumy koszyka
double ObliczSumeKoszyka(const Klient* k) {
    if (!k) return 0.0;
    double suma = 0.0;
    for (unsigned int i = 0; i < k->liczba_produktow; i++) {
        suma += k->koszyk[i].cena;
    }
    return suma;
}


//Wydrukowanie paragonu
void WydrukujParagon(const Klient* k, const char* typ_kasy, int id_kasy) {
    if (!k) return;
    
    printf("\n========== PARAGON ==========\n");
    printf("Klient ID: %d | %s %d\n", k->id, typ_kasy, id_kasy);
    printf("-----------------------------\n");
    
    for (unsigned int i = 0; i < k->liczba_produktow; i++) {
        printf("  %s (%s) - %.2f PLN\n", 
                k->koszyk[i].nazwa, 
                NazwaKategorii(k->koszyk[i].kategoria),
                k->koszyk[i].cena);
    }
    
    printf("-----------------------------\n");
    printf("SUMA: %.2f PLN | Produktow: %u\n", ObliczSumeKoszyka(k), k->liczba_produktow);
    printf("==============================\n\n");
    fflush(stdout);
}

//Handler alarmu do obslugi timeoutu
void ObslugaSIGALRM(int sig) {
    (void)sig;
    g_alarm_timeout = 1;
}

//Obsluga SIGTERM
void ObslugaSIGTERM(int sig) {
    (void)sig;

    if (!g_is_parent) {
        if (g_stan_sklepu) OdlaczPamiecWspoldzielona(g_stan_sklepu);
        _exit(1); //Kod 1 oznacza ewakuacje
    }

    //Ignorujemy SIGTERM zeby kill(0, SIGTERM) nas nie zabil
    signal(SIGTERM, SIG_IGN);
    
    //Wyslij SIGTERM do calej grupy procesow (czyli do wszystkich dzieci)
    kill(0, SIGTERM);
    
    //Czekaj na zakonczenie wszystkich dzieci i loguj
    pid_t pid_wait;
    int status;
    while ((pid_wait = wait(&status)) > 0) {
        if (WIFEXITED(status)) {
            int exit_code = WEXITSTATUS(status);
            if (exit_code == 0) ZapiszLogF(LOG_INFO, "Klient [PID: %d] zakonczyl zakupy (status: 0)", pid_wait);
            else ZapiszLogF(LOG_INFO, "Klient [PID: %d] ewakuowany (status: %d)", pid_wait, exit_code);
        } else if (WIFSIGNALED(status)) ZapiszLogF(LOG_INFO, "Klient [PID: %d] zostal zabity sygnalem %d", pid_wait, WTERMSIG(status));
    }

    if (g_stan_sklepu) OdlaczPamiecWspoldzielona(g_stan_sklepu);
}


int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uzycie: %s <pula_klientow>\n", argv[0]);
        return 1;
    }
    
    int pula_klientow = atoi(argv[1]);
    
    //Inicjalizacja systemu logowania
    InicjalizujSystemLogowania(argv[0]);
    
    //Ustawienie grupy procesow, zeby mozna bylo zabic dzieci jednym killem
    setpgid(0, 0);
    
    //Uzycie zmiennych globalnych
    if (InicjalizujProcesPochodny(&g_stan_sklepu, &g_sem_id, "Klient") == -1) {
        ZapiszLogF(LOG_BLAD, "Blad inicjalizacji procesu pochodnego");
        return 1;
    }


    struct sigaction sa;
    sa.sa_handler = ObslugaSIGTERM;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; //Flaga do przerywania
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    struct sigaction sa_alarm;
    sa_alarm.sa_handler = ObslugaSIGALRM;
    sigemptyset(&sa_alarm.sa_mask);
    sa_alarm.sa_flags = 0;
    sigaction(SIGALRM, &sa_alarm, NULL);

    while (pula_klientow-- > 0) {
        pid_t pid = fork();
        if(pid == 0) {
            //Ustawiamy flage rodzica na 0
            g_is_parent = 0;

            srand(time(NULL) ^ getpid());
            
            //Przychodzenie do sklepu w dowolnych momentach czasu (od 5s do 10min)
            int losowy_czas = 5000000 + (int)((double)rand() / RAND_MAX * 595000000);
            SYMULACJA_USLEEP(g_stan_sklepu, losowy_czas);
            
            //Czekanie na wejscie gdy jest pelny sklep
            while (ZajmijSemafor(g_sem_id, SEM_WEJSCIE_DO_SKLEPU) == -1); //To nie jest polling, semafor jest blokujacy

            //Klient wszedl
            Klient* klient = StworzKlienta(getpid());

            if (!klient) {
                ZapiszLogF(LOG_BLAD, "Blad tworzenia klienta");
                ZwolnijSemafor(g_sem_id, SEM_WEJSCIE_DO_SKLEPU);
                if (g_stan_sklepu) OdlaczPamiecWspoldzielona(g_stan_sklepu);
                exit(1);
            }

            ZapiszLogF(LOG_INFO, "Klient [ID: %d] wszedl do sklepu. Wiek: %u lat. Planuje kupic %u produktow.", klient->id, klient->wiek, klient->ilosc_planowana);

            ZapiszLog(LOG_INFO, "Klient rozpoczyna zakupy..");
            ZrobZakupy(klient, g_stan_sklepu);

            int ma_alkohol = CzyZawieraAlkohol(klient);

            //Wybieramy kase
            int wybor_kasy = rand() % 100;
            int idzie_do_samoobslugowej = (wybor_kasy < 95);

            if (idzie_do_samoobslugowej) {
                int msg_id_samo = PobierzIdKolejki(ID_IPC_SAMO);
                if (msg_id_samo != -1) {
                    MsgKasaSamo msg;

                    msg.mtype = MSG_TYPE_SAMOOBSLUGA;
                    msg.id_klienta = klient->id;
                    msg.liczba_produktow = klient->liczba_produktow;
                    msg.suma_koszyka = ObliczSumeKoszyka(klient);
                    msg.ma_alkohol = ma_alkohol;
                    msg.wiek = klient->wiek;
                    msg.timestamp = time(NULL);

                    ZapiszLogF(LOG_INFO, "Klient [ID: %d] ustawia sie w kolejce do kasy samoobslugowej.", klient->id);

                    //Ustawiamy alarm, aby dac maksymalny CZAS_OCZEKIWANIA_T na zwolnienie sie kasy samoobslugowej
                    g_alarm_timeout = 0;
                    alarm(CZAS_OCZEKIWANIA_T);

                    if (WyslijKomunikat(msg_id_samo, &msg, sizeof(MsgKasaSamo) - sizeof(long)) == 0) {
                        alarm(0); //Wylaczamy alarm po udanym wyslaniu
                        
                        //Teraz czekamy na odpowiedz
                        MsgKasaSamo res;
                        if (OdbierzKomunikat(msg_id_samo, &res, sizeof(MsgKasaSamo) - sizeof(long), MSG_RES_SAMOOBSLUGA_BASE + klient->id, 0) == 0) {
                            WydrukujParagon(klient, "Kasa Samoobslugowa", res.liczba_produktow);
                        }
                    } else {
                        alarm(0); //Wylaczamy alarm w razie bledu
                        if (errno == EINTR && g_alarm_timeout) {
                            ZapiszLogF(LOG_OSTRZEZENIE, "Klient [ID: %d] - timeout przy dolaczaniu do samoobslugi (PELNA), idzie do stacjonarnej.", klient->id);
                            idzie_do_samoobslugowej = 0;
                        } else {
                            ZapiszLogF(LOG_BLAD, "Klient [ID: %d] - blad wysylania komunikatu do kasy samoobslugowej (errno: %d)", klient->id, errno);
                        }
                    }
                }

            }/* else {
                //Kasy stacjonarne (uproszczone dla demo)
                int msg_id_kasa = PobierzIdKolejki(ID_IPC_KASA_1);
                if (msg_id_kasa != -1) {
                    MsgKasaStacj msg;
                    msg.mtype = MSG_TYPE_KASA_1;
                    msg.id_klienta = klient->id;
                    msg.liczba_produktow = klient->liczba_produktow;
                    msg.suma_koszyka = ObliczSumeKoszyka(klient);
                    msg.ma_alkohol = ma_alkohol;
                    msg.wiek = klient->wiek;
                    msg.timestamp = time(NULL);

                    if (WyslijKomunikat(msg_id_kasa, &msg, sizeof(MsgKasaStacj) - sizeof(long)) == 0) {
                        MsgKasaStacj res;
                        if (OdbierzKomunikat(msg_id_kasa, &res, sizeof(MsgKasaStacj) - sizeof(long), MSG_RES_STACJONARNA_BASE + klient->id, 0) == 0) {
                             WydrukujParagon(klient, "Kasa Stacjonarna", res.liczba_produktow + 1);
                        }
                    }
                }
            }*/



            //Wychodzi
            ZwolnijSemafor(g_sem_id, SEM_WEJSCIE_DO_SKLEPU);
            if (g_stan_sklepu) OdlaczPamiecWspoldzielona(g_stan_sklepu);
            exit(0);

        } else if (pid == -1) {
            ZapiszLogF(LOG_BLAD, "Blad fork() w procesie pochodnym");
            ObslugaSIGTERM(0);
            return 1;
        }
    }

    pid_t pid_wait;
    int status;
    while ((pid_wait = wait(&status)) > 0) {
        if (WIFEXITED(status)) {
            ZapiszLogF(LOG_INFO, "Klient [PID: %d] zakonczyl zakupy (status: %d)", pid_wait, WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            ZapiszLogF(LOG_INFO, "Klient [PID: %d] zostal zabity sygnalem %d", pid_wait, WTERMSIG(status));
        }
    }
    return 0;
}
#endif