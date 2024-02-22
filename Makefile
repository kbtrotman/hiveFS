# Compiler and linker settings
# For hiveFS.
# Nov 2, 2023
# Author: Kevin B Trotman
ccflags-y += -std=gnu11
obj-m += hivefs.o

hivefs-objs := hifs.o hi_command_kern.o hi_command_kern_memman.o hi_superblock.o hi_inode.o hi_dir.o hi_file.o hi_cache.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) clean