# Data loading from SD to DDR

This program runs on the custom board and checks that data can be loaded from
the SD card to the DDR memory.

### Getting started

1. To compile the program, run Make from the build directory:

       $ cd sd_to_ddr/build
       $ make

2. To download the program to the board, run

       $ make install

   See `uart_boot` for details on how to configure the hardware to accept the
   program bitstream over UART.

3. Monitor the UART for messages about the status of the memory test, or use the
   build in Python miniterm:

       $ make term

### Author

Jakob Kastelic, Stanford Research Systems
