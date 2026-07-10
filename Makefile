# Makefile for stm32-vserprog (STM32F4 port, separate from the F1/F0 sources)
#
# Build with:  make -C stm32f4
# Flash with:  make -C stm32f4 flash-dfu   (or flash-stlink / flash-uart)
#
# The ported sources live in this directory; the original F1/F0 sources in the
# repository root are untouched. Board definitions and the linker script are
# shared from boards/.

ROOT     ?= $(CURDIR)
PROGRAM  = stm32f4-vserprog
CROSS    ?= arm-none-eabi-
SERIAL   ?= /dev/ttyUSB0
PSERIAL  ?= /dev/ttyACM0
SPISPD   ?= 1000000000

CC       = $(CROSS)gcc
LD       = $(CROSS)ld
OBJCOPY  = $(CROSS)objcopy
OBJDUMP  = $(CROSS)objdump
SIZE     = $(CROSS)size
NM       = $(CROSS)nm

OBJS     = vserprog.o \
           usbcdc.o \
           spi.o

# STM32F4 family / board wiring (must match boards/stm32f401ce.mk)
ARCH_FLAGS = -DSTM32F4 -mthumb -mcpu=cortex-m4 -msoft-float
LDSCRIPT   = $(ROOT)/boards/ld/stm32f401xe.ld
LIBOPENCM3 = $(ROOT)/libopencm3/lib/libopencm3_stm32f4.a
OPENCM3_MK = lib/stm32/f4
BOARD_H    = $(ROOT)/boards/stm32f401ce.h

ELF      = $(PROGRAM).elf
BIN      = $(PROGRAM).bin
HEX      = $(PROGRAM).hex
MAP      = $(PROGRAM).map
DMP      = $(PROGRAM).out

CFLAGS  += -O2 -Wall -g3 -gdwarf -std=gnu99
CFLAGS  += -fno-common -ffunction-sections -fdata-sections -funit-at-a-time
CFLAGS  += -fgcse-sm -fgcse-las -fgcse-after-reload -funswitch-loops
CFLAGS  += $(ARCH_FLAGS) -I$(ROOT)/libopencm3/include/ -I$(ROOT) $(EXTRA_CFLAGS)

LIBC     = $(shell $(CC) $(CFLAGS) --print-file-name=libc.a)
LIBGCC   = $(shell $(CC) $(CFLAGS) --print-libgcc-file-name)

# LDPATH is required for libopencm3 ld scripts to work.
LDPATH   = $(ROOT)/libopencm3/lib/
LDFLAGS += -L$(LDPATH) -T$(LDSCRIPT) -Map $(MAP) --gc-sections
LDLIBS  += $(LIBOPENCM3) $(LIBC) $(LIBGCC)

.PHONY: all firmware clean flash-dfu flash-stlink flash-uart size

all: firmware

firmware: $(BIN) $(HEX) $(DMP) size

$(ELF): $(LDSCRIPT) $(OBJS) $(LIBOPENCM3) board.h
	$(LD) -o $@ $(LDFLAGS) $(OBJS) $(LDLIBS)

$(DMP): $(ELF)
	$(OBJDUMP) -d $< > $@

%.hex: %.elf
	$(OBJCOPY) -S -O ihex   $< $@

%.bin: %.elf
	$(OBJCOPY) -S -O binary $< $@

%.o: %.c board.h $(LIBOPENCM3)
	$(CC) $(CFLAGS) -c $< -o $@

board.h: $(BOARD_H)
	@ln -sfT $(BOARD_H) board.h

$(LIBOPENCM3):
	git submodule update --init
	CFLAGS="$(CFLAGS)" $(MAKE) -C $(ROOT)/libopencm3 $(OPENCM3_MK) PREFIX=$(patsubst %,%,$(CROSS)) V=1

clean:
	rm -f $(OBJS) $(ELF) $(HEX) $(BIN) $(MAP) $(DMP) board.h

flash-dfu: $(BIN)
	dfu-util -a 0 -d 0483:df11 -s 0x08000000:leave -D $<

flash-stlink: $(HEX)
	st-flash --reset --format ihex write $<

flash-uart: $(HEX)
	stm32flash -w $< -v $(SERIAL)

size: $(PROGRAM).elf
	@echo ""
	@$(SIZE) $(PROGRAM).elf
	@echo ""
