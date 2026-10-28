obj-m := blackbox.o
KDIR := /mnt/build/src/a7c-kernel/linux
PWD := $(shell pwd)
all:
	$(MAKE) -C $(KDIR) M=$(PWD) ARCH=arm CROSS_COMPILE=arm-buildroot-linux-gnueabihf- modules
