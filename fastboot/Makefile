# ZV-E10 (CXD90045, ARMv7-A) fastboot payload.
#
# No ARM GCC is installed here; Apple clang cross-compiles to armv7a-none-eabi
# and the Homebrew binutils package provides the linker and object tools.

CC      := arm-linux-gnueabihf-gcc
LD      := arm-linux-gnueabihf-gcc
OBJCOPY := arm-linux-gnueabihf-objcopy
OBJDUMP := arm-linux-gnueabihf-objdump
SIZE    := arm-linux-gnueabihf-size

TARGET  := fastboot
BUILD   := build

CFLAGS  := -march=armv7-a -mthumb -mfloat-abi=soft \
           -mno-unaligned-access -ffreestanding -fno-builtin \
           -fno-stack-protector -fno-pic -fno-common \
           -fno-asynchronous-unwind-tables -fno-unwind-tables \
           -ffunction-sections -fdata-sections \
           -Os -g -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Iinclude

# -lgcc must follow the objects, so it lives in LDPOST
LDPOST  := -lgcc

LDFLAGS := -T link.ld -nostdlib -static -no-pie -Wl,--gc-sections \
           -Wl,-Map=$(BUILD)/$(TARGET).map -Wl,--build-id=none

SRCS_C := $(wildcard src/*.c)
SRCS_S := $(wildcard src/*.S)
OBJS   := $(patsubst src/%.S,$(BUILD)/%.o,$(SRCS_S)) \
          $(patsubst src/%.c,$(BUILD)/%.o,$(SRCS_C))

.PHONY: all clean disasm size

all: $(BUILD)/$(TARGET).bin

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@
	@echo "=== $@: $$(wc -c < $@) bytes (direct serial-boot budget: 8192) ==="

$(BUILD)/$(TARGET).elf: $(OBJS) link.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS) $(LDPOST)

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/%.o: src/%.S | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD):
	mkdir -p $(BUILD)

size: $(BUILD)/$(TARGET).elf
	$(SIZE) -A $<

disasm: $(BUILD)/$(TARGET).elf
	$(OBJDUMP) -d $< > $(BUILD)/$(TARGET).disasm.txt
	@echo $(BUILD)/$(TARGET).disasm.txt

clean:
	rm -rf $(BUILD)
