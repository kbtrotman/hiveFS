# Compiler and linker settings
# For hiveFS.
# Nov 2, 2023
# Author: Kevin B Trotman
ccflags-y += -std=gnu11
obj-m += hivefs.o

hivefs-objs := hifs.o hifs_kern.o hicom_kern_mm.o hicom_kern_prot.o \
	      hifs_superblock.o hifs_inode.o hifs_dir.o hifs_file.o \
	      hifs_cache.o hifs_dedupe.o hifs_cluster.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) clean
