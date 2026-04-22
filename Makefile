CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -std=c11 -pthread -O2
TARGET  = banker
SRC     = banker.c

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

# Exemplo de execução com 3 tipos de recursos (10, 5 e 7 instâncias)
run: $(TARGET)
	./$(TARGET) 10 5 7

clean:
	rm -f $(TARGET)
