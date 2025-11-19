CC      := gcc
CFLAGS  := -std=gnu11 -Wall -Wextra -g
LDFLAGS := -lcrypto

TOOLS := mkcache.hifs get-size lscache host_register

.PHONY: all clean mkfs getsize

all: $(TOOLS)

mkcache: mkcache.hifs

getsize: get-size

lsc: lscache

host_register: register

mkcache.hifs: mkcache.hifs.o
	$(CC) $(LDFLAGS) $^ -o $@

get-size: get-size.o
	$(CC) $(LDFLAGS) $^ -o $@

mkcache.hifs.o: mkcache.hifs.c tools.h
	$(CC) $(CFLAGS) -c $< -o $@

get-size.o: get-size.c
	$(CC) $(CFLAGS) -c $< -o $@

lscache: lscache.o
	$(CC) $(LDFLAGS) $^ -o $@

lscache.o: lscache.c tools.h
	$(CC) $(CFLAGS) -c $< -o $@

register: host_register.o
	$(CC) $(LDFLAGS) $^ -o $@

register.o: host_register.c tools.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	$(RM) -f $(TOOLS) *.o
