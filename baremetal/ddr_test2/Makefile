BUILDDIR   = build
BINARYNAME = $(BUILDDIR)/main
OBJDIR     = $(BUILDDIR)/obj
OBJECTS    = $(addprefix $(OBJDIR)/, $(addsuffix .o, $(basename $(SOURCES))))

SOURCES = \
	 src/ddr_tests.c \
	 src/ddr_tool.c \
	 src/ddr_tool_util.c \
	 src/main.c \
	 src/stm32mp13xx_hal_msp.c \
	 src/syscalls.c \
	 drivers/hal/stm32mp13xx_hal.c \
	 drivers/hal/stm32mp13xx_hal_ddr.c \
	 drivers/hal/stm32mp13xx_hal_dma.c \
	 drivers/hal/stm32mp13xx_hal_gpio.c \
	 drivers/hal/stm32mp13xx_hal_i2c.c \
	 drivers/hal/stm32mp13xx_hal_i2c_ex.c \
	 drivers/hal/stm32mp13xx_hal_rcc.c \
	 drivers/hal/stm32mp13xx_hal_rcc_ex.c \
	 drivers/hal/stm32mp13xx_hal_uart.c \
	 drivers/hal/stm32mp13xx_hal_uart_ex.c \
	 drivers/CMSIS/irq_ctrl_gic.c \
	 drivers/CMSIS/startup_stm32mp135c_ca7.c \
	 drivers/CMSIS/system_stm32mp13xx_A7.c \
	 drivers/disco/stm32mp13xx_disco_bus.c \
	 drivers/disco/stm32mp13xx_disco_stpmic1.c \
	 drivers/disco/stm32mp13xx_power.c \

INCLUDES = \
	  -Isrc \
	  -Idrivers/CMSIS \
	  -Idrivers/hal \
	  -Idrivers/disco

CFLAGS = \
	 -mcpu=cortex-a7 \
	 -std=gnu11 \
	 -g1 \
	 -DSTM32MP135Cxx \
	 -DCORE_CA7 \
	 -DNO_CACHE_USE \
	 -DNO_MMU_USE \
	 -DUSE_HAL_DRIVER \
	 -DUSE_STM32MP13XX_DK \
	 -DUSE_IOEXPANDER \
	 -D__LOG_UART_IO_ \
	 -DLOGLEVEL=LOGINFO \
	 -DDDR_INTERACTIVE \
	 -DDDR_TYPE_DDR3_4Gb \
	 $(INCLUDES) \
	 -Og \
	 -ffunction-sections \
	 -Wall \
	 -Wno-strict-aliasing \
	 -MMD \
	 --specs=nano.specs \
	 -mfpu=neon-vfpv4 \
	 -mfloat-abi=hard \
	 -mthumb

LFLAGS = \
	 -mcpu=cortex-a7 \
	 -T"stm32mp13xx_a7_sysram.ld" \
	 --specs=nosys.specs \
	 -Wl,-Map,$(BINARYNAME).map,--cref \
	 -Wl,--gc-sections \
	 -static \
	 --specs=nano.specs \
	 -mfpu=neon-vfpv4 \
	 -mfloat-abi=hard \
	 -mthumb \
	 -Wl,--start-group \
	 -lc \
	 -lm \
	 -Wl,--end-group

.PHONY: all clean install read

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
