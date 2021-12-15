.POSIX:

PREFIX = /usr/local
CC = g++
CFLAGS = -std=c++23 -O3 -Wall -Wextra -Wpedantic
LIBS = -lX11 `pkg-config --libs fmt`

dwmblocks: dwmblocks.o
	$(CC) ${CFLAGS} dwmblocks.o ${LIBS} -o dwmblocks
dwmblocks.o: dwmblocks.cpp config.h
	$(CC) ${CFLAGS} -c dwmblocks.cpp
clean:
	rm -f *.o *.gch dwmblocks
install: dwmblocks
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f dwmblocks $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/dwmblocks
uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/dwmblocks

all: dwmblocks

.PHONY: clean install uninstall
