# SPDX-License-Identifier: BSD-3-Clause
# Makefile --- TODO: description
# Copyright (c) 2026 Jakob Kastelic
# Default board: EVB (STM32MP135F-DK).  Override with `make BOARD_DEFINE=`
# (empty) for the SR835-MB pinout.
BOARD_DEFINE ?= -DEVB
CFLAGS_EXTRA += $(BOARD_DEFINE)

DIRS := src drivers utils
HDR := $(wildcard $(addsuffix /*.h,$(DIRS)))
SRC := $(wildcard $(addsuffix /*.c,$(DIRS)))
OBJ := $(patsubst %.c,build/%.o,$(SRC))

CFLAGS = \
	 -Idrivers -Isrc -Iutils \
	 -D__int64_t_defined=1 \
	 -std=c99 -Wall -Wextra -Wpedantic -Wshadow -Wundef \
	 -Wmissing-prototypes -Wpointer-arith -Wfloat-equal \
	 -g3 -Os -MMD -MP \
	 -mcpu=cortex-a7 -march=armv7ve \
	 -mfpu=neon-vfpv4 -mfloat-abi=hard -mlittle-endian \
	 -fdata-sections -ffunction-sections \
	 -ffreestanding -fno-common -nostartfiles

LFLAGS = \
	 -Wl,--gc-sections \
	 -Wl,-Map,build/main.map,--cref \
	 -mcpu=cortex-a7 -march=armv7ve -mfpu=neon-vfpv4 \
	 -mlittle-endian -mfloat-abi=hard \
	 -specs=nano.specs -specs=nosys.specs \
	 -nostartfiles \
	 -ffreestanding \
	 -Wl,--print-memory-usage

all: build/main.stm32

build/main.elf: $(OBJ)
	arm-none-eabi-gcc $(LFLAGS) -T src/sysram.ld -o $@ $^

build/%.o: %.c
	mkdir -p $(dir $@)
	arm-none-eabi-gcc -c $(CPPFLAGS) $(CFLAGS) $(CFLAGS_EXTRA) $< -o $@

%.bin: %.elf
	arm-none-eabi-objcopy -O binary $< $@

%.stm32: %.bin
	python3 scripts/stm32_header.py -e $*.elf -b $< -o $@ -t .RESET

.PHONY: all clean install

clean:
	rm -rf build

install: build/main.stm32
	python3 scripts/uart_boot.py -c $(PORT) -f $<

-include $(OBJ:.o=.d)
