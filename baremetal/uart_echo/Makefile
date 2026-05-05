# SPDX-License-Identifier: BSD-3-Clause
# Makefile --- TODO: description
# Copyright (c) 2026 Jakob Kastelic
# Default board: EVB (STM32MP135F-DK).  Override with `make BOARD_DEFINE=`
# (empty) for the SR835-MB pinout.
BOARD_DEFINE ?= -DEVB

DIRS := src drivers utils
SRC  := $(wildcard $(addsuffix /*.c,$(DIRS)))
OBJ  := $(patsubst %.c,build/%.o,$(SRC))

CFLAGS = -Idrivers -Isrc -Iutils -D__int64_t_defined=1 \
         -std=c99 -Wall -Wextra -Wpedantic -Wshadow -Wundef \
         -Wmissing-prototypes -Wpointer-arith -Wfloat-equal \
         -g3 -Os -MMD -MP \
         -mcpu=cortex-a7 -march=armv7ve \
         -mfpu=neon-vfpv4 -mfloat-abi=hard -mlittle-endian \
         -fdata-sections -ffunction-sections \
         -ffreestanding -fno-common -nostartfiles \
         $(BOARD_DEFINE)

LFLAGS = -Wl,--gc-sections -Wl,-Map,build/main.map,--cref \
         -mcpu=cortex-a7 -march=armv7ve -mfpu=neon-vfpv4 \
         -mlittle-endian -mfloat-abi=hard \
         -specs=nano.specs -specs=nosys.specs \
         -nostartfiles -ffreestanding \
         -Wl,--print-memory-usage

.PHONY: all clean
.DEFAULT_GOAL := all

all: build/main.stm32

build/main.elf: $(OBJ)
	arm-none-eabi-gcc $(LFLAGS) -T src/sysram.ld -o $@ $^

build/%.o: %.c
	mkdir -p $(dir $@)
	arm-none-eabi-gcc -c $(CFLAGS) $< -o $@

%.bin: %.elf
	arm-none-eabi-objcopy -O binary $< $@

%.stm32: %.bin
	python3 scripts/stm32_header.py -e $*.elf -b $< -o $@ -t .RESET

clean:
	rm -rf build

-include $(OBJ:.o=.d)
