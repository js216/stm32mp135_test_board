# USB test

This program runs on the custom board and checks that data can be loaded from
the USB interface to the DDR memory.

Full details about the USB bring-up on the custom board are to be found
[here](https://embd.cc/usb-bringup-on-custom-stm32mp135-board).

### Getting started

To compile the program, run Make:

    $ cd usb_test
    $ make

Download to the board via JTAG or UART or even USB, and then the device will
enumerate as a USB MSC device, showing a 512MB flash drive.

### Author

Jakob Kastelic, Stanford Research Systems
