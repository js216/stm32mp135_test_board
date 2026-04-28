# uart_echo

Bare-metal UART CLI demo for the STM32MP135.  Cold-starts from USB DFU,
brings up clocks, STPMIC1, peripheral clocks, ETZPC + GPIO security off,
UART4 console at 115200 8N1, GIC, MMU.  Drops into a minimal line-edit
shell with one command (`reset`); any other input returns `Unknown
command '...'`.

### Hardware Needed

- STM32MP135F-DK Discovery board
- USB-C power cable to `CN12` (`PWR_IN`)
- USB-C cable to `CN7` (lower-left, USB DFU device)
- Micro-USB cable to `CN10` (STLink VCP, console at 115200 8N1)
- DIP switch set to `BOOT = 000` (UART/USB boot mode)

### Test Setup

Power the board by connecting USB-C to `CN12`.  Connect also the other
USB-C cable, as well as the Micro-USB cable.  Set `BOOT = 000`.  Open
the serial port to the Micro-USB cable.

### Manual Test

After flashing, the serial console shows the prompt:

```
>
```

Type characters; they should echo.  Press Enter on a non-existent
command and the shell replies with `Unknown command '...'`.  Type
`reset` to issue an MPSYSRST, or `ddrtest` to disable the cache and
check a single byte at `0xC0001000` against `0xAA`.

### Automated Test

Block 1 -- prompt comes up, characters echo, unknown command rejected.

```
mp135:uart_open
bench_mcu:reset_dut  # blobs: @main.stm32 (referenced from flash.tsv)
dfu:list
dfu:flash_layout layout=@flash.tsv no_reconnect=true
mp135:uart_expect sentinel="> " timeout_ms=15000
mp135:uart_write data="abc"
mp135:uart_expect sentinel="abc" timeout_ms=2000
mp135:uart_write data="zzznotacmd\r"
mp135:uart_expect sentinel="Unknown command" timeout_ms=2000
mp135:uart_write data="ddrtest\r"
mp135:uart_expect sentinel="SUCCESS: Byte 0 worked" timeout_ms=2000
mp135:uart_close
```

- Check `test_serv` had no errors
- Check the `> ` prompt appeared after reset
- Check typed characters echoed back to the host
- Check unknown-command line produced `Unknown command '...'`
- Check `ddrtest` reported SUCCESS at `0xC0001000`

### Author

Jakob Kastelic <jkastelic@thinksrs.com>, Stanford Research Systems, 2026.
