# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Jakob Kastelic

SHARED_APP := ../uart_echo

DIRS := src
LOCAL_SRC := \
	src/main.c \
	gpio_custom.c \
	gpio_replay_mpu_stub.c
SHARED_SRC := \
	$(SHARED_APP)/src/console.c \
	$(SHARED_APP)/src/debug.c \
	$(SHARED_APP)/src/setup.c \
	$(SHARED_APP)/utils/printf.c \
	$(SHARED_APP)/drivers/mmu_stm32mp13xx.c \
	$(SHARED_APP)/drivers/system_stm32mp13xx_A7.c \
	$(SHARED_APP)/drivers/startup_stm32mp135fxx_ca7.c \
	$(SHARED_APP)/drivers/syscalls.c \
	$(SHARED_APP)/drivers/irq_ctrl_gic.c \
	$(SHARED_APP)/drivers/stm32mp13xx_hal.c \
	$(SHARED_APP)/drivers/stm32mp13xx_hal_gpio.c \
	$(SHARED_APP)/drivers/stm32mp13xx_hal_rcc.c \
	$(SHARED_APP)/drivers/stm32mp13xx_hal_rcc_ex.c \
	$(SHARED_APP)/drivers/stm32mp13xx_hal_uart.c \
	$(SHARED_APP)/drivers/stm32mp13xx_hal_uart_ex.c
OBJ := \
	$(patsubst %.c,build/%.o,$(LOCAL_SRC)) \
	$(patsubst $(SHARED_APP)/%.c,build/shared/%.o,$(SHARED_SRC))

CFLAGS = \
	-Isrc -I. \
	-I$(SHARED_APP)/drivers \
	-I$(SHARED_APP)/src \
	-I$(SHARED_APP)/utils \
	-D__int64_t_defined=1 \
	-std=c99 -Wall -Wextra -Wpedantic -Wshadow -Wundef \
	-Wmissing-prototypes -Wpointer-arith -Wfloat-equal \
	-g3 -Os -MMD -MP \
	-mcpu=cortex-a7 -march=armv7ve \
	-mfpu=neon-vfpv4 -mfloat-abi=hard -mlittle-endian \
	-fdata-sections -ffunction-sections \
	-ffreestanding -fno-common -nostartfiles \
	$(BOARD_DEFINE)

# Board selection: `make` (default) builds the EVB image; `make BOARD=custom`
# builds for the SRS STM32MP135 PCB (flips board.h to its custom branch and
# enables the J503 SPI/QUADSPI discovery sweep in gpio_custom.c).
BOARD ?= evb
ifeq ($(BOARD),custom)
BOARD_DEFINE := -DBOARD_CUSTOM
else
BOARD_DEFINE := -DEVB
endif

LFLAGS = \
	-Wl,--gc-sections \
	-Wl,-Map,build/main.map,--cref \
	-mcpu=cortex-a7 -march=armv7ve -mfpu=neon-vfpv4 \
	-mlittle-endian -mfloat-abi=hard \
	-specs=nano.specs -specs=nosys.specs \
	-nostartfiles \
	-ffreestanding \
	-Wl,--print-memory-usage

.PHONY: all clean
.DEFAULT_GOAL := all

all: build/main.stm32 flash.tsv

build/main.elf: $(OBJ)
	arm-none-eabi-gcc $(LFLAGS) -T $(SHARED_APP)/src/sysram.ld -o $@ $^

build/%.o: %.c
	mkdir -p $(dir $@)
	arm-none-eabi-gcc -c $(CFLAGS) $< -o $@

build/shared/%.o: $(SHARED_APP)/%.c
	mkdir -p $(dir $@)
	arm-none-eabi-gcc -c $(CFLAGS) $< -o $@

%.bin: %.elf
	arm-none-eabi-objcopy -O binary $< $@

%.stm32: %.bin flash.tsv
	python3 $(SHARED_APP)/scripts/stm32_header.py -e $*.elf -b $< -o $@ -t .RESET

flash.tsv:
	printf '#Opt\tId\tName\tType\tIP\tOffset\tBinary\n-\t0x01\tfsbl1-boot\tBinary\tnone\t0x0\t@main.stm32\n' > $@

clean:
	rm -rf build

-include $(OBJ:.o=.d)
