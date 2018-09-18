PROGRAM=evproxy
LIB=-levent
CFLAGS=-Wall -static
SRC=evproxy.c

all: evproxy

evproxy: evproxy.o
	$(CC) $(SRC) $(LIB) $(CFLAGS) -o $@ 2>/dev/null

clean:
	@ rm $(PROGRAM)
