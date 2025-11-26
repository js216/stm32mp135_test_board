# Program loading from SD to DDR

This program runs on the STM32MP135F-DK evaluation board, initializing the DDR3L
memory and the SD card, then copying an executable from the SD card to the
memory, and executing it.

This program is adapted from the `FSBLA_Sdmmc1` example from the `STM32CubeMP13`
package provided by ST.

### Getting started

Pre-requisites: copy an executable program to the SD card, and adjust the
location (flash offset) and size of this executable in `main.c`:

    // copy from SD card to DDR memory
    read_sd_blocking(0x4600/BLOCKSIZE, 132);

A suitable program for execution is given in the `blink_ddr` example.

1. To compile the program, run Make from the build directory:

       $ cd sd_to_ddr/build
       $ make

2. To download the program to the board, run

       $ make install

   See `uart_boot` for details on how to configure the hardware to accept the
   program bitstream over UART.

3. Monitor the UART for messages about the status of the memory test.

### Author

Jakob Kastelic, Stanford Research Systems
