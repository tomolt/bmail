CC=clang
LD=clang
CFLAGS=-Wall -Wextra -pedantic
LDFLAGS=-g

.PHONY: all clean

all: bmaild

clean:
	rm -f *.o
	rm -f bmaild

