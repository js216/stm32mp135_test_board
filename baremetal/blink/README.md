# Blink

This program runs on a custom STM32MP135F board, blinking the red LED
(via port PA13), and printing a "Hello World" message to UART4.

Read more about the UART programmer
[here](https://embd.cc/boot-stm32mp135-over-uart-with-python).

### Getting started

1. To compile the program, run Make from this directory:

       $ cd blink
       $ make

2. To download the program to the board, run

       $ make install PORT=COM20

### Author

Jakob Kastelic, Stanford Research Systems
