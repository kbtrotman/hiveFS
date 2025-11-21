# Minimal Makefile for the wrapper.
# Adjust LIBS depending on your distro. Common combos:
#   Ubuntu/Debian: -lrocksdb -lz -lbz2 -lsnappy -llz4 -lzstd -lm -lpthread -ldl -lssl -lcrypto
#   MariaDB: -lmariadb (or -lmysqlclient on some systems)
#
#   may need pkg-config to resolve exact flags:
#   rocksdb: pkg-config --libs rocksdb
#   mariadb: mariadb_config --cflags --libs

# Minimal Makefile for the wrapper.

CC   := gcc
CXX  := g++

# ---- BRPC / BRAFT paths ----
BRPC_ROOT  := /usr/local
RAFT_ROOT := /usr/local

BRPC_INC   := $(BRPC_ROOT)/include
BRPC_LIB   := $(BRPC_ROOT)/lib

RAFT_INC  := $(RAFT_ROOT)/include
RAFT_LIB  := $(RAFT_ROOT)/lib

UV_INC := /usr/include
UV_LIB := /usr/lib

# ---- Protobuf ----
#PROTO_SRCS := hive_guard_raft.proto
#PROTO_GEN  := $(PROTO_SRCS:.proto=.pb.cc)
#PROTO_HDRS := $(PROTO_SRCS:.proto=.pb.h)

# ---- Sources ----
SRCS_C  := hive_guard.c hive_guard_tcp_coms.c hive_guard_sql.c hive_guard_erasure_coding.c hive_guard_raft.c
#SRCS_CC := hive_guard_raft_rpc.cc \
#           hive_guard_raft_statemachine.cc \
#           hive_guard_raft.cc \
#           $(PROTO_GEN)

OBJS_C  := $(SRCS_C:.c=.o)
#OBJS_CC := $(SRCS_CC:.cc=.o)
OBJS    := $(OBJS_C) $(OBJS_CC)

MAIN := hive_guard

.DEFAULT_GOAL := all

# Ensure proto headers exist before compiling anything that might include them
$(OBJS): $(PROTO_HDRS)

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
CXXFLAGS = $(WARN) $(DEBUG) $(OPT) $(INCS) -std=gnu++17

# Library search paths for linker
LDFLAGS  += -L$(BRPC_LIB) -L$(RAFT_LIB) -L$(UV_LIB)

LDLIBS   = -lbrpc -lraft -luv -lprotobuf -lgflags -lssl -lcrypto -lm -lpthread -ldl -lerasurecode

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
LIBS :=  -lleveldb    # adjust to what brpc/braft were built with and other additionals

# ---- Build ----
all: $(MAIN)

$(MAIN): $(OBJS)
# Use C++ linker so libstdc++ gets pulled in
#	$(CXX) -o $@ $(OBJS) $(LDFLAGS) $(LDLIBS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.cc
#	$(CXX) $(CXXFLAGS) -c $< -o $@

%.pb.cc %.pb.h: %.proto
#	protoc --cpp_out=. $<

clean:
	rm -f $(OBJS) $(MAIN)
