CC = gcc
# Ustawienienie standaru C17, szukanie naglowkow w folderze projektu
CFLAGS = -Wall -Wextra -std=c17 -I./Dyskont -D_GNU_SOURCE

# Biblioteki wymagane do projektu
LDFLAGS = -lpthread -lrt

TARGET = dyskont_app
SRC_DIR = Dyskont

# Automatyczne szukanie plikow .c w folderze Dyskont
SRCS = $(shell find $(SRC_DIR) -name '*.c')
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)