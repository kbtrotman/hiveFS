# Define the compiler
CC = gcc

# Includes
INCS = -I /usr/lib/gcc/x86_64-redhat-linux/8/include -I /usr/include/ -I /usr/include/libnl3/ -I /usr/include/postgresql/

# Libraries
LIBS = -L/usr/lib/ -L/usr/lib64/ -L/usr/

# Define any compile-time flags
CFLAGS = -std=gnu11 -Wall -g $(INCS) $(LIBS)

# Define the C source files
SRCS = mkfs.hifs.c

# Define the C object files 
OBJS = $(SRCS:.c=.o)

# Define the executable file 
MAIN = mkfs.hifs

all: $(MAIN)

$(MAIN): $(OBJS) 
	$(CC) $(CFLAGS) -o $(MAIN) $(OBJS)

.c.o:
	$(CC) $(CFLAGS) -c $<  -o $@

clean:
	$(RM) *.o *~ $(MAIN)