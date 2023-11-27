#
# HiveFS
#
# Hive Mind Filesystem
# By K. B. Trotman
# License: GNU GPL as of 2023
 
ccflags-y += -std=c11

obj-m = hifs.o
KDIR = /lib/modules/`uname -r`/build
SRCDIR = $(PWD)

hifs-objs += hi_superblock.o hi_inode.o hi_dir.o hi_file.o hi_cache.o hi_command_kern_netl.o

all:
	$(MAKE) -C $(KDIR) M=$(SRCDIR) modules
clean:
	$(MAKE) -C $(KDIR) M=$(SRCDIR) clean
