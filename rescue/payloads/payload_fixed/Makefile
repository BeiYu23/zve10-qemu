TC ?= /path/to/armv7-toolchain/bin/arm-buildroot-linux-gnueabihf-
CC := $(TC)gcc
OBJCOPY := $(TC)objcopy
CFLAGS := -march=armv7-a -marm -mfloat-abi=soft -nostdlib -ffreestanding -fno-builtin \
          -fno-stack-protector -fno-pic -O2 -Wall -I.
LDFLAGS := -T linker.ld -nostdlib -Wl,--build-id=none
all: payload.bin
payload.elf: start.S main.c linker.ld
	$(CC) $(CFLAGS) -o $@ start.S main.c $(LDFLAGS)
payload.bin: payload.elf
	$(OBJCOPY) -O binary $< $@
	@ls -l $@
clean:
	rm -f payload.elf payload.bin
