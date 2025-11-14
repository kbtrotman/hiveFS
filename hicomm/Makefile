CC = gcc

# ---- Sources ----
SRCS = hi_command.c hi_command_mm.c hi_command_prot.c hi_command_tcp_coms.c


OBJS = $(SRCS:.c=.o)
MAIN = hi_command

# ---- Common flags ----
CSTD    = -std=gnu11
WARN    = -Wall -Wextra
DEBUG   = -g
INCS    = -I../

CFLAGS  = $(CSTD) $(WARN) $(DEBUG) $(INCS)
LDFLAGS =
LDLIBS  = -lcrypto

# ---- Build rules ----
all: $(MAIN)

$(MAIN): $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS) $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	$(RM) $(OBJS) $(MAIN)
