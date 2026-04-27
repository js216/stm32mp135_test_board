# QSPI

Bare-register QUADSPI bring-up app for a custom STM32MP135F board.  Uses
the SR835-MB pin map by default (`-DEVB` in `CFLAGS_EXTRA` switches to the
STM32MP135F-DK CN8 mapping; see `src/board.h`).

Initialises QUADSPI at ~1 MHz, then issues JEDEC-ID reads (`0x9F` + 3 bytes
back) in a loop so the bus can be verified on a scope or logic analyser.
Prints the ID once per second over UART4 at 115200 8N1.

The QSPI driver itself (`src/qspi.c`) talks directly to the QUADSPI
registers; HAL is only used for GPIO pin-mux, RCC clock muxing, and UART
output -- the same HAL surface this project's other bare-metal apps use.

### Getting started

1. To compile the program, run Make from this directory:

       $ cd qspi
       $ make

2. To download the program to the board, run

       $ make install PORT=COM20

### Author

Jakob Kastelic, Stanford Research Systems
