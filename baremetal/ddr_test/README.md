# DDR memory test

This program runs on the STM32MP135F-DK evaluation board, initializing the DDR3L
memory, then writing a sequence of pseudo-random bits to the memory and reading
them back to verify if they were correctly written.

### Getting started

1. To compile the program, run Make from this directory:

       $ cd ddr_test
       $ make

2. To download the program to the board, run

       $ make install

3. Monitor the UART for messages about the status of the memory test.

### Author

Jakob Kastelic, Stanford Research Systems
