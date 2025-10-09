CC = gcc
CFLAGS = -Wall -std=gnu99 
LDFLAGS = -lrt
HEADERS = estructuras.h

all: coordinador generador

coordinador: coordinador.c $(HEADERS)
	$(CC) $(CFLAGS) coordinador.c -o coordinador $(LDFLAGS)

generador: generador.c $(HEADERS)
	$(CC) $(CFLAGS) generador.c -o generador $(LDFLAGS)

clean:
	rm -f coordinador generador datos_prueba.csv

.PHONY: all clean