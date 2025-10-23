CC = gcc

# Includes
INCS = -I../

# Libraries
LIBS =

# Define any compile-time flags
CFLAGS = -std=gnu11 -Wall -Wextra -g $(INCS)
LDFLAGS = $(LIBS)

MYSQL_CONFIG := $(shell command -v mysql_config 2>/dev/null)

ifeq ($(MYSQL_CONFIG),)
CFLAGS += -DHIFS_HAVE_MARIADB=0
else
MYSQL_CFLAGS := $(shell $(MYSQL_CONFIG) --cflags)
MYSQL_LIBS := $(shell $(MYSQL_CONFIG) --libs)
CFLAGS += $(MYSQL_CFLAGS) -DHIFS_HAVE_MARIADB=1
LDFLAGS += $(MYSQL_LIBS)
endif

# Define the C source files
SRCS = hi_command.c hi_command_mm.c hi_command_prot.c hi_command_sql.c

# Define the C object files 
OBJS = $(SRCS:.c=.o)

# Define the executable file 
MAIN = hi_command

all: $(MAIN)

$(MAIN): $(OBJS) 
	$(CC) $(CFLAGS) -o $(MAIN) $(OBJS) $(LDFLAGS)

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	$(RM) *.o *~ $(MAIN)
