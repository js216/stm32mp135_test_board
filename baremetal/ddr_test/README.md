# DDR memory test

This program runs on the STM32MP135F-DK evaluation board, initializing the DDR3L
memory, then writing a sequence of pseudo-random bits to the memory and reading
them back to verify if they were correctly written.

### Getting started

To compile the program, run Make from this directory:

    $ cd ddr_test
    $ make

To download the program to the board via UART, set BOOT pins to `000` and run

    $ make install PORT=COM20.

To use JTAG instead, set boot pins to `100` and use the provided GDB script to
run the code:

   # arm-none-eabi-gdb -q -x load.gdb

Monitor the UART for messages about the status of the memory test.

### Author

Jakob Kastelic, Stanford Research Systems
