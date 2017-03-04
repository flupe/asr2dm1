CFLAGS+=-Wno-unused-parameter -Wno-unused-variable
CC?=gcc
CFLAGS+=-Wextra -Wall -pedantic-errors -Werror -Wfatal-errors -Wcast-qual -Wcast-align -Wconversion -Wdouble-promotion -Wfloat-equal -Wshadow -Wpointer-arith

all: fat_cli test_utils

fat_cli: src/fat_cli.c fat32_driver.o utils.o
	$(CC) -o $@ $^ $(CFLAGS)

fat32_driver.o: src/fat32_driver.c src/fat32_driver.h
	$(CC) -o $@ -c $< $(CFLAGS)

utils.o: src/utils.c src/utils.h
	$(CC) -o $@ -c $< $(CFLAGS)

test_utils: src/test_utils.c utils.o
	$(CC) -o $@ $^ $(CFLAGS)

clean:
	rm -f *.o

.PHONY: clean
