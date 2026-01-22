CC = gcc
# Ustawienienie standaru C17, szukanie naglowkow w folderze projektu
CFLAGS = -Wall -Wextra -std=c17 -I./Dyskont -D_GNU_SOURCE

# Biblioteki wymagane do projektu
LDFLAGS = -lpthread -lrt

# Pliki wykonywalne
MAIN_TARGET = dyskont.out
KLIENT_TARGET = klient
KASJER_TARGET = kasjer
KASA_SAMO_TARGET = kasa_samoobslugowa
PRACOWNIK_TARGET = pracownik
KIEROWNIK_TARGET = kierownik

SRC_DIR = Dyskont

# Pliki wspólne dla programów (bez standalone-specific)
COMMON_OBJS_BASE = $(SRC_DIR)/pamiec_wspoldzielona.o $(SRC_DIR)/logi.o $(SRC_DIR)/semafory.o $(SRC_DIR)/kolejki.o

# Dla głównego programu - wszystkie moduły
COMMON_OBJS = $(COMMON_OBJS_BASE) $(SRC_DIR)/kasjer.o $(SRC_DIR)/kasa_samoobslugowa.o $(SRC_DIR)/pracownik_obslugi.o

# Pliki dla głównego programu (manager)
MAIN_SRCS = $(SRC_DIR)/main.c $(SRC_DIR)/klient.c
MAIN_OBJS = $(SRC_DIR)/main.o $(SRC_DIR)/klient_lib.o

# Plik dla procesu klienta
KLIENT_SRC = $(SRC_DIR)/klient.c

# Buduj wszystkie pliki wykonywalne
all: $(MAIN_TARGET) $(KLIENT_TARGET) $(KASJER_TARGET) $(KASA_SAMO_TARGET) $(PRACOWNIK_TARGET) $(KIEROWNIK_TARGET)

# Program główny (manager)
$(MAIN_TARGET): $(MAIN_OBJS) $(COMMON_OBJS)
	$(CC) $(MAIN_OBJS) $(COMMON_OBJS) -o $(MAIN_TARGET) $(LDFLAGS)

# Program klienta (standalone) - bez własnego .o w COMMON
$(KLIENT_TARGET): $(SRC_DIR)/klient_standalone.o $(COMMON_OBJS_BASE) $(SRC_DIR)/kasjer.o $(SRC_DIR)/kasa_samoobslugowa.o $(SRC_DIR)/pracownik_obslugi.o
	$(CC) $(SRC_DIR)/klient_standalone.o $(COMMON_OBJS_BASE) $(SRC_DIR)/kasjer.o $(SRC_DIR)/kasa_samoobslugowa.o $(SRC_DIR)/pracownik_obslugi.o -o $(KLIENT_TARGET) $(LDFLAGS)

# Program kasjera (standalone) - bez kasjer.o
$(KASJER_TARGET): $(SRC_DIR)/kasjer_standalone.o $(COMMON_OBJS_BASE) $(SRC_DIR)/kasa_samoobslugowa.o $(SRC_DIR)/pracownik_obslugi.o
	$(CC) $(SRC_DIR)/kasjer_standalone.o $(COMMON_OBJS_BASE) $(SRC_DIR)/kasa_samoobslugowa.o $(SRC_DIR)/pracownik_obslugi.o -o $(KASJER_TARGET) $(LDFLAGS)

# Program kasy samoobslugowej (standalone) - bez kasa_samoobslugowa.o
$(KASA_SAMO_TARGET): $(SRC_DIR)/kasa_samoobslugowa_standalone.o $(COMMON_OBJS_BASE) $(SRC_DIR)/kasjer.o $(SRC_DIR)/pracownik_obslugi.o
	$(CC) $(SRC_DIR)/kasa_samoobslugowa_standalone.o $(COMMON_OBJS_BASE) $(SRC_DIR)/kasjer.o $(SRC_DIR)/pracownik_obslugi.o -o $(KASA_SAMO_TARGET) $(LDFLAGS)

# Program pracownika obslugi (standalone) - bez pracownik_obslugi.o
$(PRACOWNIK_TARGET): $(SRC_DIR)/pracownik_standalone.o $(COMMON_OBJS_BASE) $(SRC_DIR)/kasjer.o $(SRC_DIR)/kasa_samoobslugowa.o
	$(CC) $(SRC_DIR)/pracownik_standalone.o $(COMMON_OBJS_BASE) $(SRC_DIR)/kasjer.o $(SRC_DIR)/kasa_samoobslugowa.o -o $(PRACOWNIK_TARGET) $(LDFLAGS)

# Kompilacja klient.c jako biblioteka dla main (bez KLIENT_STANDALONE)
$(SRC_DIR)/klient_lib.o: $(SRC_DIR)/klient.c
	$(CC) $(CFLAGS) -c $(SRC_DIR)/klient.c -o $(SRC_DIR)/klient_lib.o

# Kompilacja klient.c jako standalone (z KLIENT_STANDALONE)
$(SRC_DIR)/klient_standalone.o: $(SRC_DIR)/klient.c
	$(CC) $(CFLAGS) -DKLIENT_STANDALONE -c $(SRC_DIR)/klient.c -o $(SRC_DIR)/klient_standalone.o

# Kompilacja kasjer.c jako standalone (z KASJER_STANDALONE)
$(SRC_DIR)/kasjer_standalone.o: $(SRC_DIR)/kasjer.c
	$(CC) $(CFLAGS) -DKASJER_STANDALONE -c $(SRC_DIR)/kasjer.c -o $(SRC_DIR)/kasjer_standalone.o

# Kompilacja kasa_samoobslugowa.c jako standalone (z KASA_SAMO_STANDALONE)
$(SRC_DIR)/kasa_samoobslugowa_standalone.o: $(SRC_DIR)/kasa_samoobslugowa.c
	$(CC) $(CFLAGS) -DKASA_SAMO_STANDALONE -c $(SRC_DIR)/kasa_samoobslugowa.c -o $(SRC_DIR)/kasa_samoobslugowa_standalone.o

# Kompilacja pracownik_obslugi.c jako standalone (z PRACOWNIK_STANDALONE)
$(SRC_DIR)/pracownik_standalone.o: $(SRC_DIR)/pracownik_obslugi.c
	$(CC) $(CFLAGS) -DPRACOWNIK_STANDALONE -c $(SRC_DIR)/pracownik_obslugi.c -o $(SRC_DIR)/pracownik_standalone.o

# Program kierownika (standalone) - nie ma konfliktu, kierownik nie jest w COMMON
$(KIEROWNIK_TARGET): $(SRC_DIR)/kierownik_standalone.o $(COMMON_OBJS_BASE) $(SRC_DIR)/kasjer.o $(SRC_DIR)/kasa_samoobslugowa.o $(SRC_DIR)/pracownik_obslugi.o
	$(CC) $(SRC_DIR)/kierownik_standalone.o $(COMMON_OBJS_BASE) $(SRC_DIR)/kasjer.o $(SRC_DIR)/kasa_samoobslugowa.o $(SRC_DIR)/pracownik_obslugi.o -o $(KIEROWNIK_TARGET) $(LDFLAGS)

# Kompilacja kierownik.c jako standalone (z KIEROWNIK_STANDALONE)
$(SRC_DIR)/kierownik_standalone.o: $(SRC_DIR)/kierownik.c
	$(CC) $(CFLAGS) -DKIEROWNIK_STANDALONE -c $(SRC_DIR)/kierownik.c -o $(SRC_DIR)/kierownik_standalone.o

# Kompilacja pozostałych plików
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Czyszczenie
clean:
	rm -f $(SRC_DIR)/*.o $(MAIN_TARGET) $(KLIENT_TARGET) $(KASJER_TARGET) $(KASA_SAMO_TARGET) $(PRACOWNIK_TARGET) $(KIEROWNIK_TARGET)

# Uruchomienie
run: all
	./$(MAIN_TARGET)

.PHONY: all clean run
