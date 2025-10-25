CC      := gcc
CFLAGS  := -std=gnu11 -Wall -Wextra -g
LDFLAGS :=

TOOLS := mkcache.hifs get-size

.PHONY: all clean mkfs getsize

all: $(TOOLS)

mkcache: mkcache.hifs

getsize: get-size

mkcache.hifs: mkcache.hifs.o
	$(CC) $(LDFLAGS) $^ -o $@

get-size: get-size.o
	$(CC) $(LDFLAGS) $^ -o $@

mkcache.hifs.o: mkcache.hifs.c tools.h
	$(CC) $(CFLAGS) -c $< -o $@

get-size.o: get-size.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	$(RM) -f $(TOOLS) *.o
