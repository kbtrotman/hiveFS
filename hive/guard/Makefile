# Minimal Makefile for the wrapper.
# Adjust LIBS depending on your distro. Common combos:
#   Ubuntu/Debian: -lrocksdb -lz -lbz2 -lsnappy -llz4 -lzstd -lm -lpthread -ldl -lssl -lcrypto
#   MariaDB: -lmariadb (or -lmysqlclient on some systems)
#
#   may need pkg-config to resolve exact flags:
#   rocksdb: pkg-config --libs rocksdb
#   mariadb: mariadb_config --cflags --libs

CC := gcc

# ---- Sources ----      
OBJS = $(SRCS:.c=.o)
MAIN = hive_guard

# ---- Common flags ----
CSTD    = -std=gnu11
WARN    = -Wall -Wextra
DEBUG   = -g
INCS    = -I../

CFLAGS  = $(CSTD) $(WARN) $(DEBUG) $(INCS)
LDFLAGS =
LDLIBS  =
# Try to pick reasonable defaults; override by exporting LIBS in env if needed.
LIBS := -lmariadb -lz -lbz2 -lsnappy -llz4 -lzstd -lm -lpthread -ldl

# ---- MySQL ----
MYSQL_CONFIG := $(shell command -v mysql_config 2>/dev/null)
ifeq ($(MYSQL_CONFIG),)
  $(error "mysql_config not found. Please install the MariaDB/MySQL development packages.")
else
  CFLAGS  += $(shell $(MYSQL_CONFIG) --cflags)
  LDLIBS  += $(shell $(MYSQL_CONFIG) --libs) -lcrypto
endif


CFLAGS := -O2 -Wall -Wextra -std=gnu11

# ---- Build rules ----
all: $(MAIN)

$(MAIN): $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS) $(LDLIBS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	$(RM) $(OBJS) $(MAIN)

 