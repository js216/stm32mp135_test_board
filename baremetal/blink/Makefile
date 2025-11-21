BUILDDIR   = build
BINARYNAME = $(BUILDDIR)/main
OBJDIR     = $(BUILDDIR)/obj
OBJECTS    = $(addprefix $(OBJDIR)/, $(addsuffix .o, $(basename $(SOURCES))))
LINKSCR    = stm32mp13xx_a7_sysram.ld

SOURCES = \
	 src/main.c \
	 drivers/mmu_stm32mp13xx.c \
	 drivers/system_stm32mp13xx_A7.c \
	 drivers/startup_stm32mp135fxx_ca7.c \
	 drivers/syscalls.c \
	 drivers/irq_ctrl_gic.c \
	 drivers/stm32mp13xx_hal.c \
	 drivers/stm32mp13xx_hal_gpio.c \
	 drivers/stm32mp13xx_hal_rcc.c \
	 drivers/stm32mp13xx_hal_rcc_ex.c \
	 drivers/stm32mp13xx_hal_uart.c \
	 drivers/stm32mp13xx_hal_uart_ex.c \

CFLAGS = \
	 -g2 \
	 -O0 \
	 -Isrc -Idrivers \
	 -fno-common \
	 -DUSE_FULL_LL_DRIVER \
	 -DSTM32MP135Fxx \
	 -DSTM32MP1 \
	 -DCORE_CA7 \
	 -mcpu=cortex-a7 -march=armv7ve -mfpu=neon-vfpv4 -mlittle-endian -mfloat-abi=hard \
	 -fdata-sections -ffunction-sections \
	 -nostartfiles \
	 -ffreestanding \
	 $(EXTRACFLAGS)\

LFLAGS = \
	 -Wl,--gc-sections \
	 -Wl,-Map,$(BINARYNAME).map,--cref \
	 -mcpu=cortex-a7 -march=armv7ve -mfpu=neon-vfpv4 -mlittle-endian -mfloat-abi=hard \
	 -T $(LINKSCR) \
	 -specs=nano.specs -specs=nosys.specs \
	 -nostartfiles \
	 -ffreestanding \
	 -Wl,--print-memory-usage \

.PHONY: all clean install check-port

all: $(BINARYNAME).stm32

clean:
	rm -rf build

check-port:
ifndef PORT
	$(error PORT not set. Usage: make install PORT=COM1)
endif

install: $(BINARYNAME).stm32 check-port
	python3 scripts/uart_boot.py -c $(PORT) -f $<

$(OBJDIR)/%.o: %.c
	mkdir -p $(dir $@)
	arm-none-eabi-gcc -c $(CFLAGS) $< -o $@

$(BINARYNAME).elf: $(OBJECTS) $(LINKSCR)
	arm-none-eabi-gcc $(LFLAGS) -o $@ $(OBJECTS)

$(BINARYNAME).bin: $(BINARYNAME).elf
	arm-none-eabi-objcopy -O binary $< $@

$(BINARYNAME).stm32: $(BINARYNAME).bin
	python3 scripts/stm32_header.py -e $(BINARYNAME).elf -b $< -o $@ -t .RESET
