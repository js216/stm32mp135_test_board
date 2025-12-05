# USB MSC Bootloader for STM32MP135

In the STM32MP135 boot process, the ROM code loads this bootloader and if the
device is connected to a USB host, it will enumerate as USB MSC and present the
SD card to the host computer as a flash drive, to allow reflashing with `dd` and
similar tools.

If not connected to the USB host, it will copy the selected location from the SD
card to DDR, and execute it.

### Getting started

To compile the program, run Make:

    $ cd msc_boot
    $ make

Download to the board via JTAG or UART or even USB.

### Author

Jakob Kastelic, Stanford Research Systems
