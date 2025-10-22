CC = gcc

# Includes
INCS = -I../

# Libraries
LIBS =

# Define any compile-time flags
CFLAGS = -std=gnu11 -Wall -Wextra -g $(INCS)
LDFLAGS = $(LIBS)

# Define the C source files
SRCS = hi_command.c hi_command_mm.c hi_command_prot.c

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
