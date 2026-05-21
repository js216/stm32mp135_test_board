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
    JEDEC ID: 20 20 14  (NNNN reads)
    >
    JEDEC ID: 20 20 14  (NNNN reads)
    ...

Type a single-letter command (followed by space-separated decimal or
`0x`-prefixed hex args) and press Enter.

#### Discovery

| Cmd                | Description                              | Example response              |
|--------------------|------------------------------------------|-------------------------------|
| `i`                | JEDEC RDID `0x9F`, returns 3 bytes       | `JEDEC ID: 20 20 14`          |
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
| `b <len> [quad=0/1] [raw=0/1]` | Streaming bench: read N bytes (no buffer cap), validate against incrementing pattern (i & 0xFF), report KB/s + CRC32 + first-error offset.  `raw=1` skips IMODE / ADMODE and validates the received bytes directly. | `bench 1048576 B quad in 165 ms, 6362 KB/s, crc32=DEADBEEF, firsterr=-1` |
| `a [0/1]`                    | Auto-consume mode for DMA streaming work. With no arg, print state. `1` means completed DMA buffers should be consumed into CRC while DMA continues; `0` disables that path for bring-up diagnostics. Existing polling `b` and bulk-DDR `m` commands remain available either way. | `auto=on` |
| `m <len> [quad=0/1] [raw=0/1]` | Bulk MDMA read into DDR, then validate incrementing pattern and CRC32. Prints the transfer timestamp separately from validation/poison timing; `auto=` records the selected auto-consume state for streaming follow-up tests. | `mdma 1048576 B 1lane in ... auto=on` |
| `A <len> [quad=0/1] [raw=1]` | DDR streaming MDMA path. Single-lane and raw quad validate the received DDR bytes directly as the incrementing pattern (`i & 0xFF`) and compute CRC32 on those raw bytes. Raw quad prints `stream_got16` from the actual received bytes when diagnostics are available. | `stream 1048576 B quad in ... crc32=... expect=... firsterr=-1` |
| `j <len> [quad=0/1]`         | Raw data-only read (IMODE=0, ADMODE=0).  No opcode / address byte on the wire -- just `len` bytes clocked in MISO.  Cap 1024 B | `raw read 16 (1lane)` |
| `J <b0> [b1...]`             | Raw data-only write.  Bytes are clocked out MOSI with no opcode / address phase | `raw wrote 4 bytes` |

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
| `G`          | Disable QSPI; sample CLK/NCS/IO0..3 as GPIO inputs and print their levels  | `gpio read: CLK=0 NCS=1 ...`              |
| `k <n> [q=0/1] [io=1]` | Disable QSPI; bit-bang NCS/CLK as GPIO and sample `n` bytes. Single-lane samples IO`io`; quad samples IO0..3 as low nibble then high nibble | `bb read 16 (quad)` |
| `t [ms=1]`   | Run `g 0` then infinite-loop toggle all 6 between low/high.  Reset to stop | `toggle period=1 ms (reset to stop)`      |

#### Control

| Cmd  | Description                         |
|------|-------------------------------------|
| `x`  | Abort current xfer + drain RX FIFO  |
| `h`  | Print this help                     |

### Automated Test

The suite enforces real NOR-flash semantics on the QSPI slave: each
fenced block exercises an opcode sequence and the bullets assert what
the slave actually returned, not just that the host emitted the right
CLI command.  Most blocks are expected to FAIL on the bench-FPGA stub
that only echoes a fixed JEDEC ID; they will pass on a slave that
implements the JEDEC and JESD216 contracts.

Each block reflashes the MP135 via DFU and resets it.  The flash slave
itself is not power-cycled between blocks, so block N+1 sees whatever
state block N left behind.  Blocks that depend on a known starting
state begin with `e 0x06 + C + C + P` (WREN + chip erase + poll WIP)
to drive the slave to all-`ff`.  Blocks that don't write (Block 1
JEDEC, Block 3 throughput, Block 4 SFDP, Block 14 DPD, Block 23 reset
state, Block 28 back-to-back stress, etc.) skip the chip-erase
preamble.

A flash that is intentionally read-only -- for example a slave with
all status-register block-protect bits permanently set, or a
mask-ROM-style device with fixed data -- will fail the program /
erase blocks (4, 5, 8, 9, 12, 17, 18, 19, 21, 23, 31, 33, 36, 37)
and that is correct.  Such a slave is still usable as a Linux MTD
device in read-only mode; the failing blocks are the precise
conformance gap that distinguishes "read-only flash" from
"read-write flash".

Block 1 -- firmware alive, JEDEC loop, throughput.

Every block opens with `fpga:program bin=@qspi.bin` so this suite is
order-independent of any other suite (e.g. the FPGA repo's `spi`
chapter, which leaves `spi_*.bin` programmed).  test_serv only
serializes per-device, so a concurrent `run_md.py` invocation could
otherwise interleave its `fpga:program` between our blocks.

```
fpga:program bin=@qspi.bin
fpga:uart_open
mp135:uart_open
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:list
dfu:flash_layout layout=@flash.tsv no_reconnect=true
mp135:uart_expect sentinel="JEDEC ID:" timeout_ms=10000
delay ms=2500
mp135:uart_close
fpga:uart_close
```

- Check `test_serv` had no errors
- Check JEDEC ID line repeats (loop runs at least 3 times)
- Check QSPI throughput is at least 1000 reads/sec
- Check JEDEC ID is plausible (not all zero, not all ff, capacity > 0)
- Check JEDEC manufacturer is in m25p80 bank-1 list
- Check JEDEC capacity exponent indicates >= 128 KB
- Check FPGA UART captured op=9f frames from the JEDEC loop

Block 2 -- presc=1 quad diagnostic.  Drives the bench at the highest
SCLK (~328 MHz nominal, well over the chip's 166 MHz Fmax) where the
single-line bench output has historically been MISSING.  The firmware
emits `BENCHDBG ...` traces around the poll loop and `cmd_bench`
boundary; the fault handlers emit single-letter markers (U/S/P/D/R/F)
over UART4 so a CPU fault leaves visible evidence.  After the bench
window, `?` queries the CLI to confirm the firmware is still alive
and `x` aborts any in-flight transfer.  The four bullets below decode
the three failure hypotheses (poll-loop hang vs ERR-bench return vs
CPU fault) by checking which BENCHDBG markers reached the host.

```
fpga:program bin=@qspi.bin
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
fpga:uart_open
mp135:uart_open
mp135:uart_expect sentinel="JEDEC ID:" timeout_ms=10000
delay ms=200
mp135:uart_write data="p 1\r"
delay ms=200
mp135:uart_write data="b 1048576 1\r"
delay ms=12000
mp135:uart_write data="?\r"
delay ms=300
mp135:uart_write data="x\r"
delay ms=300
mp135:uart_close
fpga:uart_close
```

- Check diagnose presc=1 quad: BENCHDBG cmd_pre recorded
- Check diagnose presc=1 quad: bench function returned
- Check diagnose presc=1 quad: post-bench CLI alive
- Check diagnose presc=1 quad: no fault handler triggered

Block 2a -- raw GPIO drive/read helpers.

The `g` command is an output-side bring-up tool: it disables QUADSPI,
configures `CLK/NCS/IO0/IO1/IO2/IO3` as push-pull GPIO outputs, drives
the supplied six-bit mask, and prints the read-back level of each pin.
The `G` command is the complementary input-side probe: it disables
QUADSPI, configures the same six pins as GPIO inputs, and prints their
sampled levels.  The `k` command uses GPIO-only software NCS/CLK to read
bytes without the QUADSPI peripheral: `k <n> 0 <io>` samples one data
lane, while `k <n> 1` samples IO0..3 as low nibble then high nibble.
The input levels depend on whatever is wired to CN8, so this block only
asserts that the command paths execute and report parseable output.

```
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
mp135:uart_open
mp135:uart_expect sentinel="JEDEC ID:" timeout_ms=10000
delay ms=200
mp135:uart_write data="g 0x2a\r"
mp135:uart_expect sentinel="gpio mask=0x2a:" timeout_ms=3000
delay ms=100
mp135:uart_write data="G\r"
mp135:uart_expect sentinel="gpio read:" timeout_ms=3000
delay ms=100
mp135:uart_write data="k 4 0 1\r"
mp135:uart_expect sentinel="bb read 4 (1lane io=1)" timeout_ms=3000
delay ms=100
mp135:uart_write data="k 4 1\r"
mp135:uart_expect sentinel="bb read 4 (quad)" timeout_ms=3000
mp135:uart_close
```

- Check raw GPIO drive command reports all six pins
- Check raw GPIO read command reports all six pins
- Check bit-bang 1-lane read command prints hex dump
- Check bit-bang quad read command prints hex dump

Blocks 3-10 below are individual per-(mode, prescaler) 1 MB bench
runs.  Each fenced plan reflashes the firmware, sets one prescaler,
and runs ONE 1 MB read at ONE lane width.  This makes the early-fail
behaviour useful during FPGA bring-up: the suite stops at the first
failing (mode, prescaler) so the slave-side worker knows exactly
which case to fix next without sitting through the full sweep.

PLL4 is retuned in `src/setup.c::sysclk_init` (M=3, N=82, P=1,
integer mode) so the QSPI kernel clock is routed via PLL4P.
SCLK = ker_ck / (prescaler + 1).  CR.SSHIFT is enabled automatically
for prescaler <= 3 so MISO sampling stays inside the data window.
The slave is expected to return the incrementing pattern
(bytes 0, 1, 2, ..., 255, 0, 1, ...) over the whole address space.

Prescaler -> nominal SCLK at this PLL config:

| Prescaler | SCLK |
|-----------|------|
| 203 | ~3.22 MHz |
| 63  | ~10.25 MHz |
| 15  | ~41.0 MHz |
| 5   | ~109.3 MHz |
| 2   | ~218.7 MHz (over chip spec; usually MISSING) |
| 1   | ~328 MHz (over chip spec; usually MISSING) |

Block 11 below is the combined sweep -- runs all 6 prescalers x 2 modes
in one plan.  It is the comprehensive sanity gate that only goes green
when every individual block (3-10) plus presc=2/1 all pass.

Block 3 -- 1 MB 1-lane bench at presc=203 (~3.25 MHz wire).

```
fpga:program bin=@qspi.bin
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
fpga:uart_open
mp135:uart_open
mp135:uart_expect sentinel="JEDEC ID:" timeout_ms=10000
delay ms=200
mp135:uart_write data="p 203\r"
delay ms=100
mp135:uart_write data="b 1048576 0\r"
mp135:uart_expect sentinel="1lane @ presc=203 in" timeout_ms=20000
delay ms=300
mp135:uart_close
fpga:uart_close
```

- Check 1 MB 1lane @ presc=203 bench completes
- Check 1 MB 1lane @ presc=203 data is incrementing pattern
- Check 1 MB 1lane @ presc=203 rate within wire envelope

Block 4 -- 1 MB quad bench at presc=203.

```
fpga:program bin=@qspi.bin
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
fpga:uart_open
mp135:uart_open
mp135:uart_expect sentinel="JEDEC ID:" timeout_ms=10000
delay ms=200
mp135:uart_write data="p 203\r"
delay ms=100
mp135:uart_write data="b 1048576 1\r"
mp135:uart_expect sentinel="quad @ presc=203 in" timeout_ms=20000
delay ms=300
mp135:uart_close
fpga:uart_close
```

- Check 1 MB quad @ presc=203 bench completes
- Check 1 MB quad @ presc=203 data is incrementing pattern
- Check 1 MB quad @ presc=203 rate within wire envelope

Block 5 -- 1 MB 1-lane bench at presc=63 (~10.4 MHz wire).

```
fpga:program bin=@qspi.bin
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
fpga:uart_open
mp135:uart_open
mp135:uart_expect sentinel="JEDEC ID:" timeout_ms=10000
delay ms=200
mp135:uart_write data="p 63\r"
delay ms=100
mp135:uart_write data="b 1048576 0\r"
mp135:uart_expect sentinel="1lane @ presc=63 in" timeout_ms=15000
delay ms=300
mp135:uart_close
fpga:uart_close
```

- Check 1 MB 1lane @ presc=63 bench completes
- Check 1 MB 1lane @ presc=63 data is incrementing pattern
- Check 1 MB 1lane @ presc=63 rate within wire envelope

Block 6 -- 1 MB quad bench at presc=63.

```
fpga:program bin=@qspi.bin
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
fpga:uart_open
mp135:uart_open
mp135:uart_expect sentinel="JEDEC ID:" timeout_ms=10000
delay ms=200
mp135:uart_write data="p 63\r"
delay ms=100
mp135:uart_write data="b 1048576 1\r"
mp135:uart_expect sentinel="quad @ presc=63 in" timeout_ms=10000
delay ms=300
mp135:uart_close
fpga:uart_close
```

- Check 1 MB quad @ presc=63 bench completes
- Check 1 MB quad @ presc=63 data is incrementing pattern
- Check 1 MB quad @ presc=63 rate within wire envelope

Block 7 -- 1 MB 1-lane bench at presc=15 (~41.5 MHz wire).

```
fpga:program bin=@qspi.bin
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
fpga:uart_open
mp135:uart_open
mp135:uart_expect sentinel="JEDEC ID:" timeout_ms=10000
delay ms=200
mp135:uart_write data="p 15\r"
delay ms=100
mp135:uart_write data="b 1048576 0\r"
mp135:uart_expect sentinel="1lane @ presc=15 in" timeout_ms=10000
delay ms=300
mp135:uart_close
fpga:uart_close
```

- Check 1 MB 1lane @ presc=15 bench completes
- Check 1 MB 1lane @ presc=15 data is incrementing pattern
- Check 1 MB 1lane @ presc=15 rate within wire envelope

Block 8 -- 1 MB quad bench at presc=15.

```
fpga:program bin=@qspi.bin
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
fpga:uart_open
mp135:uart_open
mp135:uart_expect sentinel="JEDEC ID:" timeout_ms=10000
delay ms=200
mp135:uart_write data="p 15\r"
delay ms=100
mp135:uart_write data="b 1048576 1\r"
mp135:uart_expect sentinel="quad @ presc=15 in" timeout_ms=10000
delay ms=300
mp135:uart_close
fpga:uart_close
```

- Check 1 MB quad @ presc=15 bench completes
- Check 1 MB quad @ presc=15 data is incrementing pattern
- Check 1 MB quad @ presc=15 rate within wire envelope

Block 9 -- 1 MB 1-lane bench at presc=5 (~110.7 MHz wire).

```
fpga:program bin=@qspi.bin
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
fpga:uart_open
mp135:uart_open
mp135:uart_expect sentinel="JEDEC ID:" timeout_ms=10000
delay ms=200
mp135:uart_write data="p 5\r"
delay ms=100
mp135:uart_write data="b 1048576 0\r"
mp135:uart_expect sentinel="1lane @ presc=5 in" timeout_ms=10000
delay ms=300
mp135:uart_close
fpga:uart_close
```

- Check 1 MB 1lane @ presc=5 bench completes
- Check 1 MB 1lane @ presc=5 data is incrementing pattern
- Check 1 MB 1lane @ presc=5 rate within wire envelope

Block 10 -- 1 MB quad bench at presc=5.

```
fpga:program bin=@qspi.bin
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
fpga:uart_open
mp135:uart_open
mp135:uart_expect sentinel="JEDEC ID:" timeout_ms=10000
delay ms=200
mp135:uart_write data="p 5\r"
delay ms=100
mp135:uart_write data="b 1048576 1\r"
mp135:uart_expect sentinel="quad @ presc=5 in" timeout_ms=10000
delay ms=300
mp135:uart_close
fpga:uart_close
```

- Check 1 MB quad @ presc=5 bench completes
- Check 1 MB quad @ presc=5 data is incrementing pattern
- Check 1 MB quad @ presc=5 rate within wire envelope

Block 11 -- combined 1 MB sweep across all 6 prescalers x 2 modes.

```
fpga:program bin=@qspi.bin
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
fpga:uart_open
mp135:uart_open
mp135:uart_expect sentinel="JEDEC ID:" timeout_ms=10000
delay ms=200
mp135:uart_write data="p 203\r"
delay ms=100
mp135:uart_write data="b 1048576 0\r"
mp135:uart_expect sentinel="1lane @ presc=203 in" timeout_ms=20000
mp135:uart_write data="b 1048576 1\r"
mp135:uart_expect sentinel="quad @ presc=203 in" timeout_ms=20000
mp135:uart_write data="p 63\r"
delay ms=100
mp135:uart_write data="b 1048576 0\r"
mp135:uart_expect sentinel="1lane @ presc=63 in" timeout_ms=10000
mp135:uart_write data="b 1048576 1\r"
mp135:uart_expect sentinel="quad @ presc=63 in" timeout_ms=10000
mp135:uart_write data="p 15\r"
delay ms=100
mp135:uart_write data="b 1048576 0\r"
mp135:uart_expect sentinel="1lane @ presc=15 in" timeout_ms=5000
mp135:uart_write data="b 1048576 1\r"
mp135:uart_expect sentinel="quad @ presc=15 in" timeout_ms=5000
mp135:uart_write data="p 5\r"
delay ms=100
mp135:uart_write data="b 1048576 0\r"
mp135:uart_expect sentinel="1lane @ presc=5 in" timeout_ms=10000
mp135:uart_write data="b 1048576 1\r"
mp135:uart_expect sentinel="quad @ presc=5 in" timeout_ms=10000
mp135:uart_write data="p 2\r"
delay ms=100
mp135:uart_write data="b 1048576 0\r"
mp135:uart_expect sentinel="1lane @ presc=2 in" timeout_ms=10000
mp135:uart_write data="b 1048576 1\r"
mp135:uart_expect sentinel="quad @ presc=2 in" timeout_ms=10000
mp135:uart_write data="p 1\r"
delay ms=100
mp135:uart_write data="b 1048576 0\r"
mp135:uart_expect sentinel="1lane @ presc=1 in" timeout_ms=10000
mp135:uart_write data="b 1048576 1\r"
mp135:uart_expect sentinel="quad @ presc=1 in" timeout_ms=10000
mp135:uart_write data="p 203\r"
delay ms=200
mp135:uart_close
fpga:uart_close
```

- Check 1 MB scan completes 6 single-lane benches
- Check 1 MB scan completes 6 quad-lane benches
- Check 1 MB scan all bytes match incrementing pattern at every step
- Check quad-lane is faster than single-lane at every step
- Check max quad-lane throughput at the highest scanned SCLK

Block 12 -- MDMA streaming read of 16 MB into DDR at the datasheet
maximum SCLK (DS13483 Rev 5 page 170, Table 76 -- Fck1 = 166 MHz max
in SDR mode with 2.7 V <= VDD < 3.6 V and CL = 20 pF; presc=3 with
the PLL4 retune).  Exceeds SRAM (160 KB) by 100x; transfer impossible without
offloading the FIFO drain to MDMA.  MDMA channel 0 is programmed with
TSEL=0x1A (QUADSPI FIFO threshold), source = `&QUADSPI->DR` (fixed),
destination = `0xC0000000` in DDR (incrementing, 16-beat write bursts).
After the transfer the firmware walks the DDR buffer end-to-end to
compute CRC32 and the first index that violates the `i & 0xFF`
incrementing pattern.

Empirically the FPGA stub keeps up at presc=3 (~260 Mbps observed
quad), saturates at presc=2 (~221 MHz wire, over-spec for the MP135
datasheet but in spec only with VDD = 3.0-3.6 V and CL = 20 pF).

```
fpga:program bin=@qspi.bin
mp135:uart_open
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
mp135:uart_expect sentinel="JEDEC ID:" timeout_ms=10000
delay ms=200
mp135:uart_write data="p 3\r"
delay ms=200
mp135:uart_write data="m 16777216 1 1\r"
mp135:uart_expect sentinel="mdma 16777216 B" timeout_ms=60000
delay ms=300
mp135:uart_close
```

- Check 16 MB MDMA read completes
- Check 16 MB MDMA throughput at least 200 Mbps (~30% of the 664 Mbps datasheet-max bit rate: 4 lanes x 166 MHz SDR per DS13483 Rev 5 p170 Tbl 76)
- Check 16 MB MDMA data integrity into DDR
- Check `test_serv` had no errors

Block 5 -- SFDP signature, header, BFPT pointer, and BFPT contents.
Read enough SFDP bytes to cover the header (8 B), first parameter
header (8 B at offset 8), and a generous BFPT slab.  `r` caps at 1024
bytes per call, so use multiple ranged SFDP reads from offsets 0x00,
0x100, 0x200, 0x300 to gather up to 1 KB each chunk; the verifier
will pick the right slice for each parsed pointer.

```
fpga:program bin=@qspi.bin
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
fpga:uart_open
mp135:uart_open
mp135:uart_expect sentinel="JEDEC ID:" timeout_ms=10000
delay ms=300
mp135:uart_write data="i\r"
delay ms=200
mp135:uart_write data="f 0x000 256\r"
delay ms=500
mp135:uart_write data="f 0x100 256\r"
delay ms=500
mp135:uart_write data="f 0x200 256\r"
delay ms=500
mp135:uart_write data="f 0x300 256\r"
delay ms=500
mp135:uart_close
fpga:uart_close
```

- Check SFDP signature is 53 46 44 50
- Check SFDP first parameter header is BFPT (id-lsb 0x00)
- Check SFDP BFPT advertises 4 KB erase with opcode 0x20
- Check SFDP BFPT density matches JEDEC capacity
- Check SFDP BFPT has a sane QER field

Block 5 -- WEL toggles correctly via WREN/WRDI.

```
fpga:program bin=@qspi.bin
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
fpga:uart_open
mp135:uart_open
mp135:uart_expect sentinel="JEDEC ID:" timeout_ms=10000
delay ms=300
mp135:uart_write data="s 0x05\r"
delay ms=200
mp135:uart_write data="e 0x06\r"
delay ms=200
mp135:uart_write data="s 0x05\r"
delay ms=200
mp135:uart_write data="e 0x04\r"
delay ms=200
mp135:uart_write data="s 0x05\r"
delay ms=300
mp135:uart_close
fpga:uart_close
```

- Check WEL bit follows WREN and WRDI

Block 6 -- sector erase + read-back yields 0xff.

```
fpga:program bin=@qspi.bin
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
fpga:uart_open
mp135:uart_open
mp135:uart_expect sentinel="JEDEC ID:" timeout_ms=10000
delay ms=300
mp135:uart_write data="e 0x06\r"
delay ms=200
mp135:uart_write data="S 0x0\r"
delay ms=400
mp135:uart_write data="P 0x05 0x01 0x00\r"
delay ms=600
mp135:uart_write data="r 0 16\r"
delay ms=400
mp135:uart_close
fpga:uart_close
```

- Check sector erased region reads all ff

Block 7 -- page program + read-back yields the i&0xff pattern.

```
fpga:program bin=@qspi.bin
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
fpga:uart_open
mp135:uart_open
mp135:uart_expect sentinel="JEDEC ID:" timeout_ms=10000
delay ms=300
mp135:uart_write data="e 0x06\r"
delay ms=150
mp135:uart_write data="C\r"
delay ms=150
mp135:uart_write data="C\r"
delay ms=400
mp135:uart_write data="P 0x05 0x01 0x00\r"
delay ms=800
mp135:uart_write data="e 0x06\r"
delay ms=150
mp135:uart_write data="w 0 16\r"
delay ms=300
mp135:uart_write data="P 0x05 0x01 0x00\r"
delay ms=500
mp135:uart_write data="r 0 16\r"
delay ms=400
mp135:uart_close
fpga:uart_close
```

- Check page program read-back equals i and 0xff pattern

Block 8 -- quad-output read returns same data as 1-lane read.

```
fpga:program bin=@qspi.bin
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
fpga:uart_open
mp135:uart_open
mp135:uart_expect sentinel="JEDEC ID:" timeout_ms=10000
delay ms=300
mp135:uart_write data="e 0x06\r"
delay ms=150
mp135:uart_write data="C\r"
delay ms=150
mp135:uart_write data="C\r"
delay ms=400
mp135:uart_write data="P 0x05 0x01 0x00\r"
delay ms=800
mp135:uart_write data="e 0x06\r"
delay ms=150
mp135:uart_write data="w 0x100 16\r"
delay ms=300
mp135:uart_write data="P 0x05 0x01 0x00\r"
delay ms=500
mp135:uart_write data="r 0x100 16\r"
delay ms=400
mp135:uart_write data="q 0x100 16\r"
delay ms=400
mp135:uart_close
fpga:uart_close
```

- Check quad-output read matches 1-lane read at 0x100

Block 9 -- quad-I/O read returns same data as 1-lane read.

```
fpga:program bin=@qspi.bin
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
fpga:uart_open
mp135:uart_open
mp135:uart_expect sentinel="JEDEC ID:" timeout_ms=10000
delay ms=300
mp135:uart_write data="e 0x06\r"
delay ms=150
mp135:uart_write data="C\r"
delay ms=150
mp135:uart_write data="C\r"
delay ms=400
mp135:uart_write data="P 0x05 0x01 0x00\r"
delay ms=800
mp135:uart_write data="e 0x06\r"
delay ms=150
mp135:uart_write data="w 0x100 16\r"
delay ms=300
mp135:uart_write data="P 0x05 0x01 0x00\r"
delay ms=500
mp135:uart_write data="r 0x100 16\r"
delay ms=400
mp135:uart_write data="X 0x100 16\r"
delay ms=400
mp135:uart_close
fpga:uart_close
```

- Check quad-IO read matches 1-lane read at 0x100

Block 10 -- quad-input PP + read-back yields the 0xc0+i&0xf pattern.

```
fpga:program bin=@qspi.bin
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
fpga:uart_open
mp135:uart_open
mp135:uart_expect sentinel="JEDEC ID:" timeout_ms=10000
delay ms=300
mp135:uart_write data="e 0x06\r"
delay ms=150
mp135:uart_write data="C\r"
delay ms=150
mp135:uart_write data="C\r"
delay ms=400
mp135:uart_write data="P 0x05 0x01 0x00\r"
delay ms=800
mp135:uart_write data="e 0x06\r"
delay ms=150
mp135:uart_write data="Q 0x200 16\r"
delay ms=300
mp135:uart_write data="P 0x05 0x01 0x00\r"
delay ms=500
mp135:uart_write data="r 0x200 16\r"
delay ms=400
mp135:uart_close
fpga:uart_close
```

- Check quad-input PP read-back equals 0xc0+i&0xf pattern

Block 11 -- block erase 64K + read-back yields 0xff.

```
fpga:program bin=@qspi.bin
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
fpga:uart_open
mp135:uart_open
mp135:uart_expect sentinel="JEDEC ID:" timeout_ms=10000
delay ms=300
mp135:uart_write data="e 0x06\r"
delay ms=200
mp135:uart_write data="B 0x10000\r"
delay ms=400
mp135:uart_write data="P 0x05 0x01 0x00\r"
delay ms=1500
mp135:uart_write data="r 0x10000 16\r"
delay ms=400
mp135:uart_close
fpga:uart_close
```

- Check block erased region reads all ff

Block 12 -- memory-mapped read matches 1-lane read.

```
fpga:program bin=@qspi.bin
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
fpga:uart_open
mp135:uart_open
mp135:uart_expect sentinel="JEDEC ID:" timeout_ms=10000
delay ms=300
mp135:uart_write data="e 0x06\r"
delay ms=150
mp135:uart_write data="C\r"
delay ms=150
mp135:uart_write data="C\r"
delay ms=400
mp135:uart_write data="P 0x05 0x01 0x00\r"
delay ms=800
mp135:uart_write data="e 0x06\r"
delay ms=150
mp135:uart_write data="w 0x300 16\r"
delay ms=300
mp135:uart_write data="P 0x05 0x01 0x00\r"
delay ms=500
mp135:uart_write data="r 0x300 16\r"
delay ms=400
mp135:uart_write data="M 0x300 16\r"
delay ms=400
mp135:uart_close
fpga:uart_close
```

- Check memory-mapped read matches 1-lane read at 0x300

Block 13 -- autopoll reports a non-trivial wait after a real erase.

```
fpga:program bin=@qspi.bin
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
fpga:uart_open
mp135:uart_open
mp135:uart_expect sentinel="JEDEC ID:" timeout_ms=10000
delay ms=300
mp135:uart_write data="e 0x06\r"
delay ms=200
mp135:uart_write data="S 0\r"
delay ms=300
mp135:uart_write data="P 0x05 0x01 0x00\r"
delay ms=1500
mp135:uart_close
fpga:uart_close
```

- Check autopoll after sector erase reports non-zero ms

Block 14 -- chip erase + spot reads everywhere yield 0xff.

```
fpga:program bin=@qspi.bin
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
fpga:uart_open
mp135:uart_open
mp135:uart_expect sentinel="JEDEC ID:" timeout_ms=10000
delay ms=300
mp135:uart_write data="e 0x06\r"
delay ms=200
mp135:uart_write data="C\r"
delay ms=200
mp135:uart_write data="C\r"
delay ms=400
mp135:uart_write data="P 0x05 0x01 0x00\r"
delay ms=2000
mp135:uart_write data="r 0 16\r"
delay ms=300
mp135:uart_write data="r 0x10000 16\r"
delay ms=300
mp135:uart_write data="r 0x100000 16\r"
delay ms=400
mp135:uart_close
fpga:uart_close
```

- Check chip erase spot 0x0 reads all ff
- Check chip erase spot 0x10000 reads all ff
- Check chip erase spot 0x100000 reads all ff

Block 15 -- DPD silences RDID, release restores it.

```
fpga:program bin=@qspi.bin
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
fpga:uart_open
mp135:uart_open
mp135:uart_expect sentinel="JEDEC ID:" timeout_ms=10000
delay ms=300
mp135:uart_write data="i\r"
delay ms=200
mp135:uart_write data="D\r"
delay ms=300
mp135:uart_write data="i\r"
delay ms=300
mp135:uart_write data="d\r"
delay ms=300
mp135:uart_write data="i\r"
delay ms=400
mp135:uart_close
fpga:uart_close
```

- Check DPD silences JEDEC ID and release restores it

Block 16 -- soft reset clears WEL.

```
fpga:program bin=@qspi.bin
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
fpga:uart_open
mp135:uart_open
mp135:uart_expect sentinel="JEDEC ID:" timeout_ms=10000
delay ms=300
mp135:uart_write data="e 0x06\r"
delay ms=200
mp135:uart_write data="R\r"
delay ms=400
mp135:uart_write data="s 0x05\r"
delay ms=400
mp135:uart_close
fpga:uart_close
```

- Check soft reset clears WEL bit

Block 17 -- dual-output and dual-I/O reads match 1-lane read.

```
fpga:program bin=@qspi.bin
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
fpga:uart_open
mp135:uart_open
mp135:uart_expect sentinel="JEDEC ID:" timeout_ms=10000
delay ms=300
mp135:uart_write data="e 0x06\r"
delay ms=150
mp135:uart_write data="C\r"
delay ms=150
mp135:uart_write data="C\r"
delay ms=400
mp135:uart_write data="P 0x05 0x01 0x00\r"
delay ms=800
mp135:uart_write data="e 0x06\r"
delay ms=150
mp135:uart_write data="w 0x400 16\r"
delay ms=300
mp135:uart_write data="P 0x05 0x01 0x00\r"
delay ms=500
mp135:uart_write data="r 0x400 16\r"
delay ms=400
mp135:uart_write data="2 0x400 16\r"
delay ms=400
mp135:uart_write data="3 0x400 16\r"
delay ms=400
mp135:uart_close
fpga:uart_close
```

- Check dual-output read matches 1-lane read at 0x400
- Check dual-IO read matches 1-lane read at 0x400

Block 18 -- fast read 0x0B matches 1-lane read; autopoll TIMEOUT path
fires when the match condition is unreachable (WIP=1 forever).

```
fpga:program bin=@qspi.bin
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
fpga:uart_open
mp135:uart_open
mp135:uart_expect sentinel="JEDEC ID:" timeout_ms=10000
delay ms=300
mp135:uart_write data="e 0x06\r"
delay ms=150
mp135:uart_write data="C\r"
delay ms=150
mp135:uart_write data="C\r"
delay ms=400
mp135:uart_write data="P 0x05 0x01 0x00\r"
delay ms=800
mp135:uart_write data="e 0x06\r"
delay ms=150
mp135:uart_write data="w 0x500 16\r"
delay ms=300
mp135:uart_write data="P 0x05 0x01 0x00\r"
delay ms=500
mp135:uart_write data="r 0x500 16\r"
delay ms=400
mp135:uart_write data="F 0x500 16\r"
delay ms=400
mp135:uart_write data="P 0x05 0x01 0x01\r"
delay ms=6000
mp135:uart_close
fpga:uart_close
```

- Check fast read matches 1-lane read at 0x500
- Check autopoll TIMEOUT response

Block 19 -- QE bit lifecycle: setting QE in SR makes quad reads work.

```
fpga:program bin=@qspi.bin
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
fpga:uart_open
mp135:uart_open
mp135:uart_expect sentinel="JEDEC ID:" timeout_ms=10000
delay ms=300
mp135:uart_write data="e 0x06\r"
delay ms=150
mp135:uart_write data="C\r"
delay ms=150
mp135:uart_write data="C\r"
delay ms=400
mp135:uart_write data="P 0x05 0x01 0x00\r"
delay ms=800
mp135:uart_write data="e 0x06\r"
delay ms=150
mp135:uart_write data="W 0x40\r"
delay ms=300
mp135:uart_write data="P 0x05 0x01 0x00\r"
delay ms=500
mp135:uart_write data="e 0x06\r"
delay ms=150
mp135:uart_write data="w 0x600 16\r"
delay ms=300
mp135:uart_write data="P 0x05 0x01 0x00\r"
delay ms=500
mp135:uart_write data="r 0x600 16\r"
delay ms=400
mp135:uart_write data="q 0x600 16\r"
delay ms=400
mp135:uart_close
fpga:uart_close
```

- Check QE bit set then quad-output read matches 1-lane read

Block 20 -- page-program boundary wrap at offset 0xF8 across 0x100.

```
fpga:program bin=@qspi.bin
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
fpga:uart_open
mp135:uart_open
mp135:uart_expect sentinel="JEDEC ID:" timeout_ms=10000
delay ms=300
mp135:uart_write data="e 0x06\r"
delay ms=150
mp135:uart_write data="C\r"
delay ms=150
mp135:uart_write data="C\r"
delay ms=400
mp135:uart_write data="P 0x05 0x01 0x00\r"
delay ms=800
mp135:uart_write data="e 0x06\r"
delay ms=150
mp135:uart_write data="Q 0x100 16\r"
delay ms=300
mp135:uart_write data="P 0x05 0x01 0x00\r"
delay ms=500
mp135:uart_write data="e 0x06\r"
delay ms=150
mp135:uart_write data="w 0xF8 16\r"
delay ms=300
mp135:uart_write data="P 0x05 0x01 0x00\r"
delay ms=500
mp135:uart_write data="r 0xF0 32\r"
delay ms=400
mp135:uart_close
fpga:uart_close
```

- Check page-program does not cross page boundary at 0x100

Block 21 -- sector erase granularity: only the first 4K is erased.

```
fpga:program bin=@qspi.bin
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
fpga:uart_open
mp135:uart_open
mp135:uart_expect sentinel="JEDEC ID:" timeout_ms=10000
delay ms=300
mp135:uart_write data="e 0x06\r"
delay ms=150
mp135:uart_write data="C\r"
delay ms=150
mp135:uart_write data="C\r"
delay ms=400
mp135:uart_write data="P 0x05 0x01 0x00\r"
delay ms=800
mp135:uart_write data="e 0x06\r"
delay ms=150
mp135:uart_write data="w 0x0 16\r"
delay ms=300
mp135:uart_write data="P 0x05 0x01 0x00\r"
delay ms=500
mp135:uart_write data="e 0x06\r"
delay ms=150
mp135:uart_write data="Q 0x1000 16\r"
delay ms=300
mp135:uart_write data="P 0x05 0x01 0x00\r"
delay ms=500
mp135:uart_write data="e 0x06\r"
delay ms=150
mp135:uart_write data="S 0x0\r"
delay ms=400
mp135:uart_write data="P 0x05 0x01 0x00\r"
delay ms=1500
mp135:uart_write data="r 0x0 16\r"
delay ms=400
mp135:uart_write data="r 0x1000 16\r"
delay ms=400
mp135:uart_close
fpga:uart_close
```

- Check sector erase preserves adjacent 4 KB sector

Block 22 -- read-opcode parity: every read path returns the same bytes.

```
fpga:program bin=@qspi.bin
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
fpga:uart_open
mp135:uart_open
mp135:uart_expect sentinel="JEDEC ID:" timeout_ms=10000
delay ms=300
mp135:uart_write data="e 0x06\r"
delay ms=150
mp135:uart_write data="C\r"
delay ms=150
mp135:uart_write data="C\r"
delay ms=400
mp135:uart_write data="P 0x05 0x01 0x00\r"
delay ms=800
mp135:uart_write data="e 0x06\r"
delay ms=150
mp135:uart_write data="W 0x40\r"
delay ms=300
mp135:uart_write data="P 0x05 0x01 0x00\r"
delay ms=500
mp135:uart_write data="e 0x06\r"
delay ms=150
mp135:uart_write data="w 0x700 16\r"
delay ms=300
mp135:uart_write data="P 0x05 0x01 0x00\r"
delay ms=500
mp135:uart_write data="r 0x700 16\r"
delay ms=400
mp135:uart_write data="F 0x700 16\r"
delay ms=400
mp135:uart_write data="q 0x700 16\r"
delay ms=400
mp135:uart_write data="2 0x700 16\r"
delay ms=400
mp135:uart_write data="3 0x700 16\r"
delay ms=400
mp135:uart_write data="X 0x700 16\r"
delay ms=400
mp135:uart_write data="M 0x700 16\r"
delay ms=400
mp135:uart_close
fpga:uart_close
```

- Check all read opcodes return identical bytes at 0x700

Block 23 -- WIP during erase, then real wait time.

```
fpga:program bin=@qspi.bin
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
fpga:uart_open
mp135:uart_open
mp135:uart_expect sentinel="JEDEC ID:" timeout_ms=10000
delay ms=300
mp135:uart_write data="e 0x06\r"
delay ms=200
mp135:uart_write data="S 0\r"
mp135:uart_write data="s 0x05\r"
delay ms=300
mp135:uart_write data="P 0x05 0x01 0x00\r"
delay ms=2000
mp135:uart_close
fpga:uart_close
```

- Check WIP is 1 immediately after sector erase
- Check autopoll after sector erase reports >= 10 ms

Block 24 -- WRSR not persistent across soft reset.

```
fpga:program bin=@qspi.bin
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
fpga:uart_open
mp135:uart_open
mp135:uart_expect sentinel="JEDEC ID:" timeout_ms=10000
delay ms=300
mp135:uart_write data="e 0x06\r"
delay ms=200
mp135:uart_write data="W 0xBC\r"
delay ms=300
mp135:uart_write data="P 0x05 0x01 0x00\r"
delay ms=500
mp135:uart_write data="s 0x05\r"
delay ms=300
mp135:uart_write data="R\r"
delay ms=500
mp135:uart_write data="s 0x05\r"
delay ms=300
mp135:uart_close
fpga:uart_close
```

- Check WRSR value does not survive soft reset

Block 25 -- write without WREN is a no-op.

```
fpga:program bin=@qspi.bin
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
fpga:uart_open
mp135:uart_open
mp135:uart_expect sentinel="JEDEC ID:" timeout_ms=10000
delay ms=300
mp135:uart_write data="e 0x06\r"
delay ms=150
mp135:uart_write data="C\r"
delay ms=150
mp135:uart_write data="C\r"
delay ms=400
mp135:uart_write data="P 0x05 0x01 0x00\r"
delay ms=800
mp135:uart_write data="r 0x800 16\r"
delay ms=400
mp135:uart_write data="R\r"
delay ms=400
mp135:uart_write data="w 0x800 16\r"
delay ms=400
mp135:uart_write data="r 0x800 16\r"
delay ms=400
mp135:uart_close
fpga:uart_close
```

- Check page program without WREN is a no-op

Block 26 -- capacity round-trip: chip is addressable up to its declared
size.

```
fpga:program bin=@qspi.bin
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
fpga:uart_open
mp135:uart_open
mp135:uart_expect sentinel="JEDEC ID:" timeout_ms=10000
delay ms=300
mp135:uart_write data="i\r"
delay ms=200
mp135:uart_write data="e 0x06\r"
delay ms=150
mp135:uart_write data="C\r"
delay ms=150
mp135:uart_write data="C\r"
delay ms=400
mp135:uart_write data="P 0x05 0x01 0x00\r"
delay ms=2000
mp135:uart_write data="e 0x06\r"
delay ms=150
mp135:uart_write data="w 0x0 16\r"
delay ms=300
mp135:uart_write data="P 0x05 0x01 0x00\r"
delay ms=500
mp135:uart_write data="e 0x06\r"
delay ms=150
mp135:uart_write data="Q 0x40 16\r"
delay ms=300
mp135:uart_write data="P 0x05 0x01 0x00\r"
delay ms=500
mp135:uart_write data="r 0x0 16\r"
delay ms=400
mp135:uart_write data="r 0x40 16\r"
delay ms=400
mp135:uart_close
fpga:uart_close
```

- Check distinct programmed regions are independently addressable
- Check Linux readiness composite

Block 27 -- FPGA frame length matches host on JEDEC and PP.

```
fpga:program bin=@qspi.bin
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
fpga:uart_open
mp135:uart_open
mp135:uart_expect sentinel="JEDEC ID:" timeout_ms=10000
delay ms=300
mp135:uart_write data="i\r"
delay ms=200
mp135:uart_write data="e 0x06\r"
delay ms=200
mp135:uart_write data="e 0x06\r"
delay ms=200
mp135:uart_write data="w 0 16\r"
delay ms=400
mp135:uart_close
fpga:uart_close
```

- Check FPGA decoded op=9f frame length is 4
- Check FPGA decoded op=06 frame length is 1
- Check FPGA decoded op=02 frame length is 20

Block 28 -- FPGA MOSI CRC round-trip on a deterministic write.

```
fpga:program bin=@qspi.bin
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
fpga:uart_open
mp135:uart_open
mp135:uart_expect sentinel="JEDEC ID:" timeout_ms=10000
delay ms=300
mp135:uart_write data="e 0x06\r"
delay ms=200
mp135:uart_write data="e 0x06\r"
delay ms=200
mp135:uart_write data="w 0 16\r"
delay ms=500
mp135:uart_close
fpga:uart_close
```

- Check FPGA op=06 MOSI CRC matches host CRC32 of opcode-only frame
- Check FPGA op=02 MOSI CRC matches host CRC32 of opcode addr data

Block 29 -- back-to-back JEDEC reads framed independently.

```
fpga:program bin=@qspi.bin
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
fpga:uart_open
mp135:uart_open
mp135:uart_expect sentinel="JEDEC ID:" timeout_ms=10000
delay ms=300
mp135:uart_write data="i\ri\ri\r"
delay ms=400
mp135:uart_close
fpga:uart_close
```

- Check FPGA logs three op=9f frames in rapid succession
- Check FPGA op=9f frames have consistent length and CRC

Block 30 -- unsupported opcode framed without crashing the slave.

```
fpga:program bin=@qspi.bin
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
fpga:uart_open
mp135:uart_open
mp135:uart_expect sentinel="JEDEC ID:" timeout_ms=10000
delay ms=300
mp135:uart_write data="e 0x77\r"
delay ms=300
mp135:uart_write data="i\r"
delay ms=300
mp135:uart_close
fpga:uart_close
```

- Check FPGA logs op=77 frame for unsupported opcode
- Check FPGA still answers op=9f after unsupported opcode

Block 31 -- bench long-burst frame count.

```
fpga:program bin=@qspi.bin
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
fpga:uart_open
mp135:uart_open
mp135:uart_expect sentinel="JEDEC ID:" timeout_ms=10000
delay ms=300
mp135:uart_write data="b 1024 0\r"
delay ms=2000
mp135:uart_close
fpga:uart_close
```

- Check FPGA logs at least one op=03 frame from bench burst

Block 32 -- per-opcode FPGA frame length and MOSI CRC for every read
opcode.  The slave's `byte_cnt` advances every 8 SCLK rises, so a
multi-lane data phase yields fewer single-lane byte equivalents than
the on-the-wire data byte count; the MOSI CRC scope excludes any byte
clocked while the master tri-states MOSI (dummy and read-data
phases).  At addr=0, len=16 the conformance values are:
op=9f bytes=4 crc=crc32([0x9F]);
op=0b bytes=21 crc=crc32([0x0B,0,0,0]);
op=6b bytes=9 crc=crc32([0x6B,0,0,0]);
op=3b bytes=13 crc=crc32([0x3B,0,0,0]);
op=bb bytes=10 crc=crc32([0xBB,0,0,0,0xA0]);
op=eb bytes=6 crc=crc32([0xEB,0,0,0,0xA0]).

```
fpga:program bin=@qspi.bin
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
fpga:uart_open
mp135:uart_open
mp135:uart_expect sentinel="JEDEC ID:" timeout_ms=10000
delay ms=300
mp135:uart_write data="i\r"
delay ms=200
mp135:uart_write data="F 0 16\r"
delay ms=400
mp135:uart_write data="q 0 16\r"
delay ms=400
mp135:uart_write data="2 0 16\r"
delay ms=400
mp135:uart_write data="3 0 16\r"
delay ms=400
mp135:uart_write data="X 0 16\r"
delay ms=500
mp135:uart_close
fpga:uart_close
```

- Check FPGA op=9f MOSI CRC matches host CRC32 of opcode-only frame
- Check FPGA op=0b frame bytes=21 and CRC matches opcode plus address
- Check FPGA op=6b frame bytes=9 and CRC matches opcode plus address
- Check FPGA op=3b frame bytes=13 and CRC matches opcode plus address
- Check FPGA op=bb frame bytes=10 and CRC matches opcode plus address plus alt
- Check FPGA op=eb frame bytes=6 and CRC matches opcode plus address plus alt

Block 33 -- WEL auto-clears on successful page-program completion.
A real flash drops both WIP and WEL when PP finishes; the host
should see WEL=1 after WREN, WEL=1 (with WIP=1) during PP, and
WEL=0 (with WIP=0) once autopoll exits.

```
fpga:program bin=@qspi.bin
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
fpga:uart_open
mp135:uart_open
mp135:uart_expect sentinel="JEDEC ID:" timeout_ms=10000
delay ms=300
mp135:uart_write data="e 0x06\r"
delay ms=150
mp135:uart_write data="C\r"
delay ms=150
mp135:uart_write data="C\r"
delay ms=400
mp135:uart_write data="P 0x05 0x01 0x00\r"
delay ms=800
mp135:uart_write data="e 0x06\r"
delay ms=200
mp135:uart_write data="s 0x05\r"
delay ms=200
mp135:uart_write data="w 0 16\r"
delay ms=400
mp135:uart_write data="P 0x05 0x01 0x00\r"
delay ms=600
mp135:uart_write data="s 0x05\r"
delay ms=400
mp135:uart_close
fpga:uart_close
```

- Check WEL set after WREN before PP
- Check WEL auto-clears after PP completes

Block 34 -- mid-frame CS deassert does not crash or mis-frame.
Driving CLK partway through a byte then raising NCS must reset
the slave's capture state cleanly.  No bogus frame should be
emitted, and a follow-up `i` must produce a normal op=9f frame.

```
fpga:program bin=@qspi.bin
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
fpga:uart_open
mp135:uart_open
mp135:uart_expect sentinel="JEDEC ID:" timeout_ms=10000
delay ms=300
mp135:uart_write data="i\r"
delay ms=300
mp135:uart_write data="g 0x02\r"
delay ms=100
mp135:uart_write data="g 0x00\r"
delay ms=50
mp135:uart_write data="g 0x01\r"
delay ms=50
mp135:uart_write data="g 0x00\r"
delay ms=50
mp135:uart_write data="g 0x01\r"
delay ms=50
mp135:uart_write data="g 0x00\r"
delay ms=50
mp135:uart_write data="g 0x01\r"
delay ms=50
mp135:uart_write data="g 0x00\r"
delay ms=50
mp135:uart_write data="g 0x01\r"
delay ms=50
mp135:uart_write data="g 0x02\r"
delay ms=200
mp135:uart_write data="p 203\r"
delay ms=400
mp135:uart_write data="i\r"
delay ms=400
mp135:uart_close
fpga:uart_close
```

- Check FPGA frame count unchanged across the partial-byte CS abort
- Check FPGA op=9f frame appears after the CS-abort sequence

Block 35 -- page buffer wraps at the 16-byte boundary on PP.
Pre-program 0x00..0x0F with 0xAA, then PP 8 bytes starting at
0x0F.  The slave's address-low[3:0] index wraps, so byte 0 of the
write lands at 0x0F and bytes 1..7 wrap back to 0x00..0x06.

```
fpga:program bin=@qspi.bin
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
fpga:uart_open
mp135:uart_open
mp135:uart_expect sentinel="JEDEC ID:" timeout_ms=10000
delay ms=300
mp135:uart_write data="e 0x06\r"
delay ms=150
mp135:uart_write data="C\r"
delay ms=150
mp135:uart_write data="C\r"
delay ms=400
mp135:uart_write data="P 0x05 0x01 0x00\r"
delay ms=800
mp135:uart_write data="e 0x06\r"
delay ms=150
mp135:uart_write data="W 0xAA\r"
delay ms=300
mp135:uart_write data="P 0x05 0x01 0x00\r"
delay ms=400
mp135:uart_write data="e 0x06\r"
delay ms=150
mp135:uart_write data="w 0x0F 8\r"
delay ms=400
mp135:uart_write data="P 0x05 0x01 0x00\r"
delay ms=500
mp135:uart_write data="r 0 16\r"
delay ms=400
mp135:uart_close
fpga:uart_close
```

- Check page buffer wraps within the 16-byte boundary on PP

Block 36 -- sustained back-to-back JEDEC stress.  100 `i` keystrokes
fire as one UART burst; the slave must frame each one independently.

```
fpga:program bin=@qspi.bin
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
fpga:uart_open
mp135:uart_open
mp135:uart_expect sentinel="JEDEC ID:" timeout_ms=10000
delay ms=300
mp135:uart_write data="i\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\ri\r"
delay ms=4000
mp135:uart_close
fpga:uart_close
```

- Check FPGA logs at least 100 op=9f frames in a sustained burst
- Check every op=9f frame in the burst has bytes=4 and matching CRC

Block 37 -- mixed-opcode burst interleaves WREN and JEDEC.

```
fpga:program bin=@qspi.bin
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
fpga:uart_open
mp135:uart_open
mp135:uart_expect sentinel="JEDEC ID:" timeout_ms=10000
delay ms=300
mp135:uart_write data="i\re 0x06\ri\re 0x06\ri\re 0x06\ri\re 0x06\ri\re 0x06\ri\re 0x06\ri\re 0x06\ri\re 0x06\ri\re 0x06\ri\re 0x06\ri\re 0x06\ri\re 0x06\ri\re 0x06\ri\re 0x06\ri\re 0x06\ri\re 0x06\ri\re 0x06\ri\re 0x06\ri\re 0x06\ri\re 0x06\ri\re 0x06\ri\re 0x06\ri\re 0x06\ri\re 0x06\ri\re 0x06\r"
delay ms=4000
mp135:uart_close
fpga:uart_close
```

- Check FPGA logs at least 25 op=9f and 25 op=06 frames in the mix
- Check mixed-burst op=9f frames are bytes=4 and op=06 frames are bytes=1

Block 38 -- WIP polled mid page-program reports busy.  Real flash
drives WIP=1 throughout the PP cycle (~ms range); a stubbed slave
that completes instantly fails this assertion.

```
fpga:program bin=@qspi.bin
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
fpga:uart_open
mp135:uart_open
mp135:uart_expect sentinel="JEDEC ID:" timeout_ms=10000
delay ms=300
mp135:uart_write data="e 0x06\r"
delay ms=200
mp135:uart_write data="w 0 16\r"
mp135:uart_write data="s 0x05\r"
delay ms=300
mp135:uart_write data="P 0x05 0x01 0x00\r"
delay ms=500
mp135:uart_close
fpga:uart_close
```

- Check WIP is 1 immediately after page program kickoff
- Check autopoll after page program reports >= 1 ms

Block 39 -- quad-bench throughput floor at high SCLK.  Re-init at
prescaler 1 (peak SCLK on this PLL config), run a 4 KB quad bench,
parse the rate line, then restore safe speed.

```
fpga:program bin=@qspi.bin
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:flash_layout layout=@flash.tsv no_reconnect=true
fpga:uart_open
mp135:uart_open
mp135:uart_expect sentinel="JEDEC ID:" timeout_ms=10000
delay ms=300
mp135:uart_write data="p 1\r"
delay ms=300
mp135:uart_write data="b 4096 1\r"
mp135:uart_expect sentinel="quad @ presc=1" timeout_ms=5000
mp135:uart_write data="p 203\r"
delay ms=300
mp135:uart_close
fpga:uart_close
```

- Check quad bench rate is at least 4 MB/s
- Check quad bench CRC32 matches the QPP read-back pattern

### Author

Jakob Kastelic, Stanford Research Systems
