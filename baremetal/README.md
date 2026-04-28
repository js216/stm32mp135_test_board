# Bare-metal projects

Each subdirectory is a self-contained bare-metal app for the STM32MP135.  All
files needed to build are inside the subdirectory.  See the top-level
[`README.md`](../README.md) for what each project does.

### Build

From the project directory:

    make CFLAGS_EXTRA=-DEVB     # STM32MP135F-DK Discovery board
    make                        # custom SR835-MB board (default)

Output is `build/main.stm32`.

### Manual flash + run

`BOOT = 000` puts the BootROM into UART/USB boot mode.  Power the EVB via
`CN12` (`PWR_IN` USB-C) and connect `CN7` (USB-C) for DFU.  Flash via
STM32CubeProgrammer:

    STM32_Programmer_CLI.exe -c port=usb1 -w flash.tsv

The image runs from SYSRAM as soon as the download finishes.  Console is
UART4 over the STLink VCP on `CN10` (Micro-USB), 115200 8N1.  Per-project
expected output is described in each project's `README.md`.

### Automated test

The bench uses [`test_serv`](https://github.com/js216/test_serv) to drive the
EVB through hardware reset, USB DFU flash, and UART capture.  Each project's
`README.md` has an `### Automated Test` section: a fenced plan block (op lines)
followed by a bulleted check list.  Run from the project directory, e.g.:

    cd qspi
    python3 /path/to/test_serv/run_md.py

`run_md.py` reads `README.md`, packages the plan together with the blobs it
references (`@name` tokens; resolved from the project directory or its
`build/` subdirectory), submits the job, waits for completion, then runs
`verify.py` against the captured streams once per bullet.  Each bullet maps
to a function in `verify.py`'s `DISPATCH`.
