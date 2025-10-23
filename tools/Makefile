CC      := gcc
CFLAGS  := -std=gnu11 -Wall -Wextra -g
LDFLAGS :=

TOOLS := mkfs.hifs get-size

.PHONY: all clean mkfs getsize

all: $(TOOLS)

mkfs: mkfs.hifs

getsize: get-size

mkfs.hifs: mkfs.hifs.o
	$(CC) $(LDFLAGS) $^ -o $@

get-size: get-size.o
	$(CC) $(LDFLAGS) $^ -o $@

mkfs.hifs.o: mkfs.hifs.c tools.h
	$(CC) $(CFLAGS) -c $< -o $@

get-size.o: get-size.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	$(RM) -f $(TOOLS) *.o
