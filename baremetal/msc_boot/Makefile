PORT = COM20

BINARYNAME = build/main
OBJDIR     = build/obj
LINKSCR    = src/stm32mp13xx_a7_sysram.ld

SRC := $(wildcard src/*.c drivers/*.c nonfree/*.c)
OBJ := $(patsubst %.c,$(OBJDIR)/%.o,$(SRC))

CFLAGS = \
	-DCORE_CA7 \
	-DDDR_TYPE_DDR3_4Gb \
	-DSTM32MP1 -DSTM32MP135Fxx \
	-DUSE_FULL_LL_DRIVER \
	-Idrivers -Inonfree -Isrc \
	-g2 -O3 \
	-mcpu=cortex-a7 -march=armv7ve \
	-mfpu=neon-vfpv4 -mfloat-abi=hard -mlittle-endian \
	-fdata-sections -ffunction-sections \
	-ffreestanding -fno-common -nostartfiles

LFLAGS = \
	 -Wl,--gc-sections \
	 -Wl,-Map,$(BINARYNAME).map,--cref \
	 -mcpu=cortex-a7 -march=armv7ve -mfpu=neon-vfpv4 -mlittle-endian -mfloat-abi=hard \
	 -T $(LINKSCR) \
	 -specs=nano.specs -specs=nosys.specs \
	 -nostartfiles \
	 -ffreestanding \
	 -Wl,--print-memory-usage \

# Build recipes

all: $(BINARYNAME).stm32

$(OBJDIR)/%.o: %.c
	mkdir -p $(dir $@)
	arm-none-eabi-gcc -c $(CFLAGS) $< -o $@

$(BINARYNAME).elf: $(OBJ) $(LINKSCR)
	arm-none-eabi-gcc $(LFLAGS) -o $@ $(OBJ)

$(BINARYNAME).bin: $(BINARYNAME).elf
	arm-none-eabi-objcopy -O binary $< $@

$(BINARYNAME).stm32: $(BINARYNAME).bin
	python3 scripts/stm32_header.py -e $(BINARYNAME).elf -b $< -o $@ -t .RESET

# Testing and linting

check: format cppcheck tidy inclusions

format:
	clang-format --dry-run -Werror $(SRC)

tidy:
	make clean
	mkdir -p build
	bear --output build/compile_commands.json -- make -j$(shell nproc) $(TARGET)
	run-clang-tidy -j$(shell nproc) -p build $(SRC)

cppcheck:
	cppcheck --enable=all --inconclusive --std=c++17 --force --quiet \
		--error-exitcode=1 --suppress=missingInclude src

inclusions: $(INC_SRCS) scripts/inclusions.py
	python3 scripts/inclusions.py $(SRC) $(HDRS) > build/incl.dot ; \
	PYTHON_EXIT=$$? ; \
	dot -Tpdf build/incl.dot -o build/incl.pdf ; \
	exit $$PYTHON_EXIT

# UART bootloader

term:
	python3 -m serial.tools.miniterm $(PORT) 115200

install: $(BINARYNAME).stm32
	python3 scripts/uart_boot.py -c $(PORT) -f $<

# General

.PHONY: all clean install term

clean:
	rm -rf build
