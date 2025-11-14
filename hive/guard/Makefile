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
SRCS := hive_guard.c hive_guard_tcp_coms.c hive_guard_sql.c hive_guard_erasure_coding.c
OBJS := $(SRCS:.c=.o)

MAIN := hive_guard

# ---- Flags ----
CSTD    = -std=gnu11
WARN    = -Wall -Wextra
DEBUG   = -g
OPT     = -O2
INCS    = -I../

CFLAGS  = $(CSTD) $(WARN) $(DEBUG) $(OPT) $(INCS)
LDFLAGS =
LDLIBS  =

# ---- MariaDB / MySQL flags ----
MYSQL_CONFIG := $(shell command -v mysql_config 2>/dev/null)
ifeq ($(MYSQL_CONFIG),)
  $(error "mysql_config not found. Please install libmariadb-dev or libmysqlclient-dev.")
else
  CFLAGS  += $(shell $(MYSQL_CONFIG) --cflags)
  LDLIBS  += $(shell $(MYSQL_CONFIG) --libs) -lcrypto
endif

# Additional libs (adjust if you add RocksDB/etc later)
LIBS := -lm -lpthread -ldl -lerasurecode

# ---- Build ----
all: $(MAIN)

$(MAIN): $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS) $(LDLIBS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(MAIN)
