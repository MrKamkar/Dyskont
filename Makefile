CC = gcc
# Ustawienienie standaru C17, szukanie naglowkow w folderze projektu
CFLAGS = -Wall -Wextra -std=c17 -I./Dyskont -D_GNU_SOURCE

# Biblioteki wymagane do projektu
LDFLAGS = -lpthread -lrt

# Pliki wykonywalne
MAIN_TARGET = dyskont.out
KLIENT_TARGET = klient

SRC_DIR = Dyskont

# Pliki wspólne dla obu programów
COMMON_SRCS = $(SRC_DIR)/pamiec_wspoldzielona.c $(SRC_DIR)/logi.c $(SRC_DIR)/semafory.c
COMMON_OBJS = $(COMMON_SRCS:.c=.o)

# Pliki dla głównego programu (manager)
MAIN_SRCS = $(SRC_DIR)/main.c $(SRC_DIR)/klient.c
MAIN_OBJS = $(SRC_DIR)/main.o $(SRC_DIR)/klient_lib.o

# Plik dla procesu klienta
KLIENT_SRC = $(SRC_DIR)/klient.c

# Buduj oba pliki wykonywalne
all: $(MAIN_TARGET) $(KLIENT_TARGET)

# Program główny (manager)
$(MAIN_TARGET): $(MAIN_OBJS) $(COMMON_OBJS)
	$(CC) $(MAIN_OBJS) $(COMMON_OBJS) -o $(MAIN_TARGET) $(LDFLAGS)

# Program klienta (standalone)
$(KLIENT_TARGET): $(SRC_DIR)/klient_standalone.o $(COMMON_OBJS)
	$(CC) $(SRC_DIR)/klient_standalone.o $(COMMON_OBJS) -o $(KLIENT_TARGET) $(LDFLAGS)

# Kompilacja klient.c jako biblioteka dla main (bez KLIENT_STANDALONE)
$(SRC_DIR)/klient_lib.o: $(SRC_DIR)/klient.c
	$(CC) $(CFLAGS) -c $(SRC_DIR)/klient.c -o $(SRC_DIR)/klient_lib.o

# Kompilacja klient.c jako standalone (z KLIENT_STANDALONE)
$(SRC_DIR)/klient_standalone.o: $(SRC_DIR)/klient.c
	$(CC) $(CFLAGS) -DKLIENT_STANDALONE -c $(SRC_DIR)/klient.c -o $(SRC_DIR)/klient_standalone.o

# Kompilacja pozostałych plików
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Czyszczenie
clean:
	rm -f $(SRC_DIR)/*.o $(MAIN_TARGET) $(KLIENT_TARGET)

# Uruchomienie
run: all
	./$(MAIN_TARGET)

.PHONY: all clean run