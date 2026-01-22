#include "klient.h"
#include "pamiec_wspoldzielona.h"
#include "kasjer.h"
#include "kasa_samoobslugowa.h"
#include "kolejki.h"
#include <sys/wait.h>
#include <pthread.h>

#ifdef KLIENT_STANDALONE

//Zmienne globalne dla obslugi sygnalow
static StanSklepu* g_stan_sklepu = NULL;
static int g_sem_id = -1;
static int g_czy_rodzic = 1; //Domyslnie jestesmy rodzicem (Generatorem)
static int g_cichy_tryb = 0; //Tryb cichy - brak paragonow na konsoli

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

        //Znajdz produkty z kategorii, ktorych klient jeszcze nie ma
        int dostepne_indeksy[MAX_PRODUKTOW];
        int liczba_dostepnych = 0;

        for (unsigned int i = 0; i < stan_sklepu->liczba_produktow; i++) {
            if (!CzyMaKategorie(k, stan_sklepu->magazyn[i].kategoria)) {
                dostepne_indeksy[liczba_dostepnych++] = i;
            }
        }

        //Wybor produktu
        int indeks = 0;
        if (liczba_dostepnych > 0) {
            //Wybierz losowy produkt z nowej kategorii
            indeks = dostepne_indeksy[rand() % liczba_dostepnych];
        } else {
            //Jesli klient ma juz wszystkie kategorie (niemozliwe przy obecnych ustawieniach)
            indeks = rand() % stan_sklepu->liczba_produktow;
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
    if (!k || g_cichy_tryb) return;
    
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


//Flaga dla SIGUSR1 - wybudzenie z msgrcv przez watek sprawdzajacy
static volatile sig_atomic_t g_wycofaj_do_stacjonarnej = 0;

//Handler SIGUSR1 - sygnal od watku sprawdzajacego wolna kase stacjonarna
void ObslugaSIGUSR1(int sig) {
    (void)sig;
    g_wycofaj_do_stacjonarnej = 1;
}

//Struktura argumentow dla watku sprawdzajacego
typedef struct {
    int id_klienta;
    pid_t pid_klienta;
    StanSklepu* stan_sklepu;
    int sem_id;
} WatekSprawdzajacyArgs;

//Watek sprawdzajacy wolna kase stacjonarna po T sekundach
void* WatekSprawdzajacyKase(void* arg) {
    WatekSprawdzajacyArgs* args = (WatekSprawdzajacyArgs*)arg;
    
    //Blokujemy sygnaly w tym watku
    sigset_t set;
    sigfillset(&set);
    pthread_sigmask(SIG_BLOCK, &set, NULL);
    
    //Czekamy T sekund
    sleep(CZAS_OCZEKIWANIA_T);
    
    //Sprawdzamy czy jest wolna kasa stacjonarna
    ZajmijSemafor(args->sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    
    //Czy klient jest juz obslugiwany przez kase samoobslugowa?
    int jest_obslugiwany = 0;
    for (int i = 0; i < LICZBA_KAS_SAMOOBSLUGOWYCH; i++) {
        if (args->stan_sklepu->kasy_samoobslugowe[i].id_klienta == args->id_klienta) {
            jest_obslugiwany = 1;
            break;
        }
    }

    int wolna_kasa = 0;
    if (!jest_obslugiwany) {
        for (int i = 0; i < LICZBA_KAS_STACJONARNYCH; i++) {
            if (args->stan_sklepu->kasy_stacjonarne[i].stan == KASA_WOLNA) {
                    wolna_kasa = 1;
                    break;
                }
            }
    } else {
        ZapiszLogF(LOG_DEBUG, "Watek: Klient [ID: %d] jest w trakcie obslugi, anuluje wycofanie.", args->id_klienta);
    }
    
    ZwolnijSemafor(args->sem_id, MUTEX_PAMIEC_WSPOLDZIELONA);
    
    if (wolna_kasa) {
        //Dodaj klienta do tablicy pomijanych
        DodajPomijanego(args->stan_sklepu, args->sem_id, args->id_klienta);
        
        ZapiszLogF(LOG_INFO, "Watek: Klient [ID: %d] wycofany do kasy stacjonarnej (wolna kasa).", args->id_klienta);
        
        //Wyslij sygnal SIGUSR1 do klienta, zeby przerwac msgrcv
        kill(args->pid_klienta, SIGUSR1);
    }
    
    free(args);
    return NULL;
}

//Obsluga SIGTERM
void ObslugaSIGTERM(int sig) {
    (void)sig;

    if (!g_czy_rodzic) {
        if (g_stan_sklepu) OdlaczPamiecWspoldzielona(g_stan_sklepu);
        _exit(1); //Kod 1 oznacza ewakuacje
    }

    //Rodzic ignoruje kolejne sygnaly
    signal(SIGTERM, SIG_IGN);
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    
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
    
    //Sprawdzenie argumentu -quiet (przekazany przez main.c)
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-quiet") == 0) {
            g_cichy_tryb = 1;
            break;
        }
    }
    
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

    struct sigaction sa_sigusr1;
    sa_sigusr1.sa_handler = ObslugaSIGUSR1;
    sigemptyset(&sa_sigusr1.sa_mask);
    sa_sigusr1.sa_flags = 0;
    sigaction(SIGUSR1, &sa_sigusr1, NULL);

    while (pula_klientow-- > 0) {
        pid_t pid = fork();
        if(pid == 0) {
            //Ustawiamy flage rodzica na 0
            g_czy_rodzic = 0;

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

            //Sygnal dla watku managera kas samoobslugowych ze klient wszedl
            ZwolnijSemafor(g_sem_id, SEM_NOWY_KLIENT);

            ZapiszLog(LOG_INFO, "Klient rozpoczyna zakupy..");
            ZrobZakupy(klient, g_stan_sklepu);

            //Wybieramy kase
            int wybor_kasy = rand() % 100;
            int idzie_do_samoobslugowej = (wybor_kasy < 95);
            int obsluzony = 0;  //Flaga czy klient zostal obsluzony

            if (idzie_do_samoobslugowej) {
                int msg_id_samo = PobierzIdKolejki(ID_IPC_SAMO);
                if (msg_id_samo != -1) {
                    MsgKasaSamo msg;

                    msg.mtype = MSG_TYPE_SAMOOBSLUGA;
                    msg.id_klienta = klient->id;
                    msg.liczba_produktow = klient->liczba_produktow;
                    msg.suma_koszyka = ObliczSumeKoszyka(klient);
                    msg.ma_alkohol = CzyZawieraAlkohol(klient);
                    msg.wiek = klient->wiek;
                    msg.timestamp = time(NULL);

                    ZapiszLogF(LOG_INFO, "Klient [ID: %d] ustawia sie w kolejce do kasy samoobslugowej.", klient->id);

                    //Utworzenie watku sprawdzajacego wolna kase stacjonarna
                    pthread_t watek_sprawdzajacy;
                    int watek_utworzony = 0;
                    WatekSprawdzajacyArgs* args = malloc(sizeof(WatekSprawdzajacyArgs));
                    
                    if (args) {
                        args->id_klienta = klient->id;
                        args->pid_klienta = getpid();
                        args->stan_sklepu = g_stan_sklepu;
                        args->sem_id = g_sem_id;
                        
                        g_wycofaj_do_stacjonarnej = 0;
                        if (pthread_create(&watek_sprawdzajacy, NULL, WatekSprawdzajacyKase, args) == 0) {
                            watek_utworzony = 1;
                        } else {
                            ZapiszLogF(LOG_BLAD, "Klient [ID: %d] - blad tworzenia watku sprawdzajacego (errno: %d)", klient->id, errno);
                            free(args);
                        }
                    } else {
                        ZapiszLogF(LOG_BLAD, "Klient [ID: %d] - brak pamieci dla watku sprawdzajacego", klient->id);
                    }

                    int sukces = 0;
                    int id_kasy_samo = -1;
                    if (WyslijKomunikat(msg_id_samo, &msg, sizeof(MsgKasaSamo) - sizeof(long), g_sem_id, SEM_KOLEJKA_SAMO) == 0) {
                        //Czekamy na odpowiedz blokujaco
                        MsgKasaSamo res;
                        int wynik = OdbierzKomunikat(msg_id_samo, &res, sizeof(MsgKasaSamo) - sizeof(long), MSG_RES_SAMOOBSLUGA_BASE + klient->id, 0, g_sem_id, SEM_KOLEJKA_SAMO);
                        
                        if (wynik == 0) {
                            id_kasy_samo = res.liczba_produktow;
                            //W res.id_klienta jest kod wyniku operacji (0 = OK)
                            if (res.id_klienta == 0) {
                                sukces = 1;
                            } else if (res.id_klienta == -2) {
                                ZapiszLogF(LOG_OSTRZEZENIE, "Klient [ID: %d] niepelnoletni (probowal kupic alkohol). Opuszcza sklep bez zakupow.", klient->id);
                                obsluzony = 1; //Traktujemy jako "obsluzonego"
                            } else {
                                ZapiszLogF(LOG_OSTRZEZENIE, "Klient [ID: %d] nieobsluzony w kasie samoobslugowej (kod: %d).", klient->id, res.id_klienta);
                            }
                        }
                    }
                    
                    //Anulowanie watku sprawdzajacego (jesli jeszcze dziala i byl utworzony)
                    if (watek_utworzony) {
                        pthread_cancel(watek_sprawdzajacy);
                        pthread_join(watek_sprawdzajacy, NULL);
                    }
                    
                    if (sukces) {
                        //Sukces - drukujemy paragon i konczymy
                        WydrukujParagon(klient, "Kasa Samoobslugowa", id_kasy_samo + 1);
                        obsluzony = 1; //KLUCZOWE: Oznacz ze klient zostal obsluzony!
                    } else if (g_wycofaj_do_stacjonarnej) {
                        //Watek sprawdzajacy wyslal sygnal - idziemy do stacjonarnej
                        ZapiszLogF(LOG_OSTRZEZENIE, "Klient [ID: %d] - wolna kasa stacjonarna, wycofuje sie z samoobslugowej.", klient->id);
                        idzie_do_samoobslugowej = 0;
                    } else if (errno == EINTR) {
                        //Inny sygnal (np. SIGTERM)
                        ZapiszLogF(LOG_BLAD, "Klient [ID: %d] - przerwano oczekiwanie (errno: %d)", klient->id, errno);
                    }
                }


            }
            
            //Jesli nie obsluzony przy samoobslugowej - idz do stacjonarnej
            if (!obsluzony) {
                //Kasy stacjonarne - klient trafia do wspolnej kolejki
                int msg_id_wspolna = PobierzIdKolejki(ID_IPC_KASA_WSPOLNA);
                
                if (!idzie_do_samoobslugowej) {
                    ZapiszLogF(LOG_INFO, "Klient [ID: %d] ustawia sie we wspolnej kolejce do kas stacjonarnych.", klient->id);
                }
                
                MsgKasaStacj msg;
                msg.mtype = MSG_TYPE_KASA_WSPOLNA;
                msg.id_klienta = klient->id;
                msg.liczba_produktow = klient->liczba_produktow;
                msg.suma_koszyka = ObliczSumeKoszyka(klient);
                msg.ma_alkohol = CzyZawieraAlkohol(klient);
                msg.wiek = klient->wiek;

                if (WyslijKomunikat(msg_id_wspolna, &msg, sizeof(MsgKasaStacj) - sizeof(long), g_sem_id, SEM_KOLEJKA_WSPOLNA) == 0) {
                    MsgKasaStacj res;
                    size_t msg_size = sizeof(MsgKasaStacj) - sizeof(long);
                    long mtype_res = MSG_RES_STACJONARNA_BASE + klient->id;
                    
                    //Czekamy blokujaco na odpowiedz ze wspolnej kolejki
                    if (OdbierzKomunikat(msg_id_wspolna, &res, msg_size, mtype_res, 0, g_sem_id, SEM_KOLEJKA_WSPOLNA) == 0) {
                        ZapiszLogF(LOG_INFO, "Klient [ID: %d] zakonczyl zakupy przy kasie stacjonarnej %d.", klient->id, res.liczba_produktow + 1);
                        WydrukujParagon(klient, "Kasa Stacjonarna", res.liczba_produktow + 1);
                    }
                }
            }



            //Wychodzi - sygnalizujemy watkowi skalujacemu kas samoobslugowych
            ZwolnijSemafor(g_sem_id, SEM_NOWY_KLIENT);
            ZwolnijSemafor(g_sem_id, SEM_WEJSCIE_DO_SKLEPU);
            if (g_stan_sklepu) OdlaczPamiecWspoldzielona(g_stan_sklepu);
            exit(0);

        } else if (pid == -1) {
            ZapiszLogF(LOG_BLAD, "Blad fork() w generatorze klientow");
            ObslugaSIGTERM(0); //Zabij wszystkie dotychczasowe dzieci i wyjdz
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
