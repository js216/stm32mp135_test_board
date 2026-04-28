# QSPI

Bare-metal QUADSPI bring-up app for the STM32MP135.  Cold-starts from USB
DFU, brings up clocks, STPMIC1, peripheral clocks, ETZPC + GPIO security
off, UART4 console at 115200 8N1, GIC.  Configures QUADSPI at ~1 MHz
(prescaler 203) and runs an interactive single-letter shell over UART4
that exercises every command the Linux `m25p80` driver would issue.  In
the background (between CLI commands) the app keeps issuing JEDEC reads
and printing the result once per second, so a passive monitor can confirm
the bus is alive.

The QUADSPI peripheral is driven through raw register writes; HAL is only
used for GPIO pin-mux and clock enables.  Pin map per board in
[`src/board.h`](src/board.h).  DMA is intentionally not implemented.

The driver supports indirect read/write, memory-mapped read (Bank 1 at
`0x70000000`), auto-poll status, DDR mode (`CCR.DDRM`), and dual-flash
mode (`CR.DFM`).

### Hardware Needed

- STM32MP135F-DK Discovery board
- USB-C power cable to `CN12` (`PWR_IN`)
- USB-C cable to `CN7` (lower-left, USB DFU device)
- Micro-USB cable to `CN10` (STLink VCP, console at 115200 8N1)
- DIP switch set to `BOOT = 000` (UART/USB boot mode)

### Test Setup

Power the board by connecting USB-C to `CN12`. Connect also the other
USB-C cable, as well as the Micro-USB cable. Set `BOOT = 000`. Open
serial port to the Micro-USB cable.

Connect a flash or flash-emulating FPGA to the QSPI pins on `CN8`
(40-pin RPi-compatible header on the EVB).  The slave's chip-select,
clock, and four data lanes go to:

| QSPI signal | MP135 pin | CN8 connector | Direction (1-lane) |
|-------------|-----------|---------------|---------------------|
| `CLK`       | `PF10`    | CN8.26        | host -> slave       |
| `NCS`       | `PD1`     | CN8.5         | host -> slave       |
| `IO0`       | `PH3`     | CN8.19        | host -> slave (MOSI)|
| `IO1`       | `PF9`     | CN8.33        | slave -> host (MISO)|
| `IO2`       | `PH6`     | CN8.3         | bidir (quad only)   |
| `IO3`       | `PH7`     | CN8.23        | bidir (quad only)   |

In dual / quad modes `IO0..IO3` are all bidirectional.  Tie `GND`
to any of CN8.6/9/14/20/25/30/34/39, and pull `3V3` from CN8.1
(or CN8.17) if the slave needs power from the EVB.

**Conflict warning:** `NCS` (PD1) and `IO2` (PH6) are also `I2C5_SCL`
/ `I2C5_SDA` for the on-board capacitive touch panel (CTP).  The QSPI
bring-up app reconfigures these pins for QUADSPI, so the CTP stops
working.  If LCD/touch is glued or wired to the same connector,
disconnect it before testing.

### Manual Test

After flashing, the serial console prints:

    qspi bring-up
    qspi: presc=203 fsize=23 csht=1
    >
    JEDEC ID: aa 55 01  (NNNN reads)
    >
    JEDEC ID: aa 55 01  (NNNN reads)
    ...

Type a single-letter command (followed by space-separated decimal or
`0x`-prefixed hex args) and press Enter.

#### Discovery

| Cmd                | Description                              | Example response              |
|--------------------|------------------------------------------|-------------------------------|
| `i`                | JEDEC RDID `0x9F`, returns 3 bytes       | `JEDEC ID: aa 55 01`          |
| `f <addr> <len>`   | SFDP `0x5A` + 24-bit addr + 8 dummy      | `SFDP @ 0x00000000 (16 bytes)` |
| `s [opc=0x05]`     | Opcode-then-1-byte (default RDSR)        | `op=05 -> ff`                 |
| `?`                | busy / prescaler / fsize / CS-high cycles| `busy=0 presc=203 fsize=23 csht=1` |

#### Reads

| Cmd                          | Description                                      | Example                    |
|------------------------------|--------------------------------------------------|----------------------------|
| `r <addr> <len> [opc=0x03]`  | 1-lane read (default `0x03`).  Cap 1024 B        | `op=03 read 16 @ 0x0`      |
| `F <addr> <len>`             | Fast read `0x0B`, 8 dummy cycles                 | `op=0b read 16 @ 0x0`      |
| `q <addr> <len>`             | Quad-output read `0x6B`, 4-lane data             | `op=6b read 16 @ 0x0`      |
| `X <addr> <len>`             | Quad-I/O read `0xEB`, alt-byte `0xA0`, 4 dummy   | `op=eb read 16 @ 0x0`      |
| `2 <addr> <len>`             | Dual-output read `0x3B`, 2-lane data, 8 dummy    | `op=3b read 16 @ 0x0`      |
| `3 <addr> <len>`             | Dual-I/O read `0xBB`, alt-byte `0xA0`, 4 dummy   | `op=bb read 16 @ 0x0`      |
| `M <addr> <len>`             | Memory-mapped read at `0x70000000+addr`          | `MM read 16 @ 0x0`         |
| `b <len> [quad=0/1]`         | Bench: time read (1-lane or quad-output), throughput, CRC32, first-error | `bench 256 B 1lane in N ms, ...` |

#### Writes / Erase

| Cmd                          | Description                                | Example                              |
|------------------------------|--------------------------------------------|--------------------------------------|
| `e [opc=0x06]`               | Opcode-only (default WREN)                 | `op=06 sent`                         |
| `W <val> [opc=0x01]`         | Write status reg: opcode + 1-byte data, no addr | `op=01 wrote SR=02`              |
| `w <addr> <len> [opc=0x02]`  | 1-lane page program; cap 256 B; pat = i & 0xFF | `op=02 wrote 16 bytes @ 0x0`     |
| `Q <addr> <len>`             | Quad-input PP `0x32`; pat = `0xC0 + (i&0x0F)` | `op=32 wrote 16 bytes @ 0x0`      |
| `S <addr>`                   | Sector erase 4 KB (`0x20`)                 | `op=20 erased 4 KB @ 0x0`            |
| `B <addr>`                   | Block erase 64 KB (`0xD8`)                 | `op=d8 erased 64 KB @ 0x0`           |
| `C`                          | Chip erase (`0xC7`); confirm by re-typing `C` within 2 s | `Type C again to confirm.` |
| `P [op] [mask] [match]`      | Auto-poll status (default WIP=0)           | `autopoll op=05 mask=01 match=00 done in N ms` |

#### Addressing / Power / Mode

| Cmd          | Description                                         | Example                |
|--------------|-----------------------------------------------------|------------------------|
| `4 [0/1]`    | Exit (`0xE9`) or enter (`0xB7`) 4-byte addr; toggles tracked state with no arg | `4byte=1 (op=b7)` |
| `R`          | Soft reset (`0x66` then `0x99`)                     | `reset (66 99) sent`   |
| `D`          | Deep power-down (`0xB9`)                            | `op=b9 deep power-down`|
| `d`          | Release from DPD (`0xAB`)                           | `op=ab release DPD`    |
| `p <n>`      | Set prescaler, re-init keeping fsize/csht           | `presc=10`             |

#### Bring-up Helpers (raw GPIO, peripheral disabled)

| Cmd          | Description                                                                | Example                                   |
|--------------|----------------------------------------------------------------------------|-------------------------------------------|
| `g <mask6>`  | Disable QSPI; force CLK/NCS/IO0..3 to GPIO output and drive bits 0..5      | `gpio mask=0x3f: CLK=1 NCS=1 ...`         |
| `t [ms=1]`   | Run `g 0` then infinite-loop toggle all 6 between low/high.  Reset to stop | `toggle period=1 ms (reset to stop)`      |

#### Control

| Cmd  | Description                         |
|------|-------------------------------------|
| `x`  | Abort current xfer + drain RX FIFO  |
| `h`  | Print this help                     |

### Automated Test

```
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:list
dfu:flash_layout layout=@flash.tsv no_reconnect=true
mp135:uart_open
mp135:uart_expect sentinel="JEDEC ID:" timeout_ms=10000
delay ms=2500
mp135:uart_close
```

- Check `test_serv` had no errors
- Check JEDEC ID line repeats (loop runs at least 3 times)
- Check QSPI throughput is at least 1000 reads/sec

```
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
mp135:uart_open
delay ms=500
mp135:uart_write data="h\r"
delay ms=300
mp135:uart_write data="i\r"
delay ms=200
mp135:uart_write data="?\r"
delay ms=200
mp135:uart_write data="s 0x05\r"
delay ms=200
mp135:uart_write data="e 0x06\r"
delay ms=300
mp135:uart_close
```

- Check help output present
- Check JEDEC response pattern
- Check busy status query
- Check status read
- Check WREN op response

```
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
mp135:uart_open
delay ms=500
mp135:uart_write data="r 0 16\r"
delay ms=300
mp135:uart_write data="F 0 16\r"
delay ms=300
mp135:uart_write data="q 0 16\r"
delay ms=300
mp135:uart_write data="X 0 16\r"
delay ms=400
mp135:uart_write data="2 0 16\r"
delay ms=300
mp135:uart_write data="3 0 16\r"
delay ms=400
mp135:uart_close
```

- Check 1-lane read response
- Check fast read response
- Check quad-output read response
- Check quad-IO read response
- Check dual-output read response
- Check dual-IO read response

```
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
mp135:uart_open
delay ms=500
mp135:uart_write data="e 0x06\r"
delay ms=200
mp135:uart_write data="W 0x02\r"
delay ms=300
mp135:uart_write data="w 0 16\r"
delay ms=400
mp135:uart_write data="Q 0 16\r"
delay ms=400
mp135:uart_write data="r 0 16\r"
delay ms=400
mp135:uart_write data="S 0\r"
delay ms=400
mp135:uart_write data="B 0x10000\r"
delay ms=400
mp135:uart_close
```

- Check write op response
- Check quad write op response
- Check sector erase response
- Check block erase response
- Check WRSR response

```
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
mp135:uart_open
delay ms=500
mp135:uart_write data="f 0 16\r"
delay ms=400
mp135:uart_close
```

- Check SFDP response

```
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
mp135:uart_open
delay ms=500
mp135:uart_write data="M 0 16\r"
delay ms=500
mp135:uart_close
```

- Check memory-mapped response

```
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
mp135:uart_open
delay ms=500
mp135:uart_write data="b 256 0\r"
delay ms=500
mp135:uart_write data="b 256 1\r"
delay ms=500
mp135:uart_close
```

- Check bench 1-lane output present
- Check bench quad output present

```
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
mp135:uart_open
delay ms=500
mp135:uart_write data="D\r"
delay ms=200
mp135:uart_write data="d\r"
delay ms=200
mp135:uart_write data="R\r"
delay ms=300
mp135:uart_close
```

- Check DPD response
- Check release DPD response
- Check soft reset response

```
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
mp135:uart_open
delay ms=500
mp135:uart_write data="4 1\r"
delay ms=200
mp135:uart_write data="4 0\r"
delay ms=300
mp135:uart_close
```

- Check 4-byte enter response
- Check 4-byte exit response

```
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
mp135:uart_open
delay ms=500
mp135:uart_write data="g 0x3f\r"
delay ms=300
mp135:uart_write data="g 0\r"
delay ms=300
mp135:uart_close
```

- Check gpio set high response
- Check gpio set low response

```
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
mp135:uart_open
delay ms=500
mp135:uart_write data="e 0x06\r"
delay ms=200
mp135:uart_write data="P 0x05 0x01 0x00\r"
delay ms=500
mp135:uart_close
```

- Check autopoll response

```
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
mp135:uart_open
delay ms=500
mp135:uart_write data="P 0x05 0x01 0x01\r"
delay ms=6000
mp135:uart_close
```

- Check autopoll timeout response

```
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
mp135:uart_open
delay ms=500
mp135:uart_write data="C\r"
delay ms=200
mp135:uart_write data="C\r"
delay ms=400
mp135:uart_close
```

- Check chip erase confirms
- Check chip erase executed

### Author

Jakob Kastelic, Stanford Research Systems
