# Compiler and linker settings
# For hiveFS.
# Nov 2, 2023
# Author: Kevin B Trotman
ccflags-y += -std=gnu11 -Wno-declaration-after-statement
obj-m += hifs.o

hifs-objs := hi_superblock.o hi_inode.o hi_dir.o hi_file.o hi_command_kern_netl.o hi_cache.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) clean