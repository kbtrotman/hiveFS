# Minimal Makefile for the wrapper.
# Adjust LIBS depending on distro. Common combos:
#   Ubuntu/Debian: -lrocksdb -lz -lbz2 -lsnappy -llz4 -lzstd -lm -lpthread -ldl -lssl -lcrypto
#   MariaDB: -lmariadb (or -lmysqlclient on some systems)
#
#   may need pkg-config to resolve exact flags:
#   rocksdb: pkg-config --libs rocksdb
#   mariadb: mariadb_config --cflags --libs

# Minimal Makefile for the wrapper.

CC   := gcc

# ---- BRPC / BRAFT paths ----
BRPC_ROOT  := /usr/local
RAFT_ROOT := /usr/local

BRPC_INC   := $(BRPC_ROOT)/include
BRPC_LIB   := $(BRPC_ROOT)/lib

RAFT_INC  := $(RAFT_ROOT)/include
RAFT_LIB  := $(RAFT_ROOT)/lib

UV_INC := /usr/include
UV_LIB := /usr/lib

# ---- Sources ----
SRCS_C  := hive_bootstrap.c hive_bootstrap_sql.c


OBJS_C  := $(SRCS_C:.c=.o)

MAIN := hive_bootstrap

.DEFAULT_GOAL := all

# ---- Flags ----
CSTD    = -std=gnu11
WARN    = -Wall -Wextra
DEBUG   = -g
OPT     = -O2
INCS    = -I../ \
          -I$(BRPC_INC) \
          -I$(RAFT_INC) \
          -I$(UV_INC)

CFLAGS   = $(CSTD) $(WARN) $(DEBUG) $(OPT) $(INCS)

# Library search paths for linker
LDFLAGS  += -L$(BRPC_LIB) -L$(RAFT_LIB) -L$(UV_LIB)

LDLIBS   = -lbrpc -luv -lgflags -lssl -lcrypto -lm -lpthread -ldl

# ---- MariaDB / MySQL flags ----
MYSQL_CONFIG := $(shell command -v mysql_config 2>/dev/null)
ifeq ($(MYSQL_CONFIG),)
  $(error "mysql_config not found. Please install libmariadb-dev or libmysqlclient-dev.")
else
  CFLAGS   += $(shell $(MYSQL_CONFIG) --cflags)
 # CXXFLAGS += $(shell $(MYSQL_CONFIG) --cflags)
  LDLIBS   += $(shell $(MYSQL_CONFIG) --libs) -lcrypto
endif

# Additional libs
LIBS :=  -lrocksdb    # adjust to what brpc/braft were built with and other additionals

# ---- Build ----
all: $(MAIN)

$(MAIN): $(OBJS_C)
	$(CC) -o $@ $(OBJS_C) $(LDFLAGS) $(LDLIBS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@


clean:
	rm -f $(OBJS_C) $(MAIN)
