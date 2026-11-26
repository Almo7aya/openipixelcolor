# openipixelcolor firmware build.  Usage:  make            (needs nds32le-elf-gcc on PATH, or CROSS=...)
#                                  make CROSS=/path/to/nds32le-elf-
CROSS   ?= nds32le-elf-
CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
OBJDUMP := $(CROSS)objdump
SIZE    := $(CROSS)size
PYTHON  ?= python3

BUILD   := build
TARGET  := $(BUILD)/fw

# Core is full AndeStar V3 (r15-r27, bitci, bset, pop25 are all available). cmodel=medium: data/bss
# gp-relative, rodata absolute (small would make flash rodata gp-relative = out of range).
ARCH_FLAGS ?= -mlittle-endian -march=v3 -mcmodel=medium -mno-fp-as-gp
# -MMD -MP: every object also depends on the headers it includes (a header change rebuilds its users)
# EXTRA_CFLAGS: added at the end (the CI passes -Werror)
CFLAGS  := $(ARCH_FLAGS) -Os -g -std=gnu11 -Wall -Wextra -ffreestanding -fno-builtin \
           -ffunction-sections -fdata-sections -Isrc -MMD -MP $(EXTRA_CFLAGS)
ASFLAGS := $(ARCH_FLAGS) -g
LDFLAGS := $(ARCH_FLAGS) -T link.ld -nostartfiles -nostdlib -Wl,--gc-sections -Wl,--no-relax \
           -Wl,-Map=$(TARGET).map

SRCS_C  := $(wildcard src/*.c src/hal/*.c src/lib/*.c src/drv/*.c src/app/*.c)
SRCS_S  := $(wildcard src/*.S)
OBJS    := $(patsubst src/%,$(BUILD)/%,$(SRCS_C:.c=.o) $(SRCS_S:.S=.o))

all: $(TARGET).bin $(TARGET).ota.bin

# flash driver runs from the cache-locked window with interrupts off: no jump tables in .rodata,
# no compiler-generated memcpy/memset calls out of the window
$(BUILD)/drv/iflash.o: CFLAGS += -fno-jump-tables -fno-tree-loop-distribute-patterns

$(BUILD)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: src/%.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJS) link.ld
	$(CC) $(LDFLAGS) $(OBJS) -lgcc -o $@
	$(SIZE) $@

# raw image starting at 0x10000 (what goes into flash at 0x10000)
$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@
	$(OBJDUMP) -d $< > $(TARGET).lst
	$(PYTHON) -I ../tools/check_image.py $@ --listing $(TARGET).lst

# update file for the Bluetooth updater (byte-substituted, the format the panel's updater expects)
$(TARGET).ota.bin: $(TARGET).bin
	$(PYTHON) -I ../tools/fwcodec.py encode $< $@ -f
	$(PYTHON) -I ../tools/fwcodec.py info $@

-include $(OBJS:.o=.d)

disasm: $(TARGET).elf
	$(OBJDUMP) -d $< > $(TARGET).lst

clean:
	rm -rf $(BUILD)

.PHONY: all clean disasm
