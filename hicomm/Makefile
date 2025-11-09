CC = gcc

# ---- Sources ----
SRCS = hi_command.c hi_command_mm.c hi_command_prot.c hi_command_sql.c \
       hifs_erasure_coding.c         


OBJS = $(SRCS:.c=.o)
MAIN = hi_command

# ---- Common flags ----
CSTD    = -std=gnu11
WARN    = -Wall -Wextra
DEBUG   = -g
INCS    = -I../

CFLAGS  = $(CSTD) $(WARN) $(DEBUG) $(INCS)
LDFLAGS =
LDLIBS  =

# ---- MySQL ----
MYSQL_CONFIG := $(shell command -v mysql_config 2>/dev/null)
ifeq ($(MYSQL_CONFIG),)
  $(error "mysql_config not found. Please install the MariaDB/MySQL development packages.")
else
  CFLAGS  += $(shell $(MYSQL_CONFIG) --cflags)
  LDLIBS  += $(shell $(MYSQL_CONFIG) --libs) -lcrypto
endif

# ---- liberasurecode via pkg-config, with fallback ----
HAVE_EC_PC := $(shell pkg-config --exists erasurecode && echo yes || echo no)

ifeq ($(HAVE_EC_PC),yes)
  CFLAGS  += $(shell pkg-config --cflags erasurecode)
  LDLIBS  += $(shell pkg-config --libs   erasurecode)
else
  # Fallback to /usr/local (adjust if you installed elsewhere)
  EC_PREFIX ?= /usr/local
  CFLAGS  += -I$(EC_PREFIX)/include
  LDLIBS  += -L$(EC_PREFIX)/lib -lerasurecode -ldl
  # Optional: help pkg-config find erasurecode.pc next time
  # export PKG_CONFIG_PATH := $(EC_PREFIX)/lib/pkgconfig:$(PKG_CONFIG_PATH)
endif

# ---- Build rules ----
all: $(MAIN)

$(MAIN): $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS) $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	$(RM) $(OBJS) $(MAIN)

