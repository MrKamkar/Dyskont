#include <stdio.h>

int main(int argc, char* argv[]) {

    printf("Hello World!\n");

    // Wypisanie informacji o liczbie argumentów
    printf("Liczba argumentow: %d\n", argc);

    // Pêtla wypisuj¹ca ka¿dy argument w nowej linii
    for (int i = 0; i < argc; i++) {
        printf("Argument %d: %s\n", i, argv[i]);
    }

    return 0;
}