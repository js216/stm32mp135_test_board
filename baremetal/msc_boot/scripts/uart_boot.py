# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025 Stanford Research Systems, Inc.

import math
import struct
import pyvisa
import argparse

def pack_cmd(cmd):
    if cmd not in [0x00, 0x01, 0x02, 0x03, 0x11, 0x12, 0x21, 0x31]:
        raise RuntimeError("Invalid cmd requested.")
    # command followed by its complement
    return struct.pack("BB", cmd, 0xff-cmd)


def interp_byte(b):
    if b == 0x79:
        return "ACK"
    elif b == 0x1F:
        return "NACK"
    elif b == 0x5F:
        return "ABORT"
    else:
        return format(b, '#04x')


def get_ack(dev, note="", do_print=False):
    try:
        r = dev.read_bytes(1)[0]
    except pyvisa.errors.VisaIOError:
        # try again
        r = dev.read_bytes(1)[0]

    if r == 0:
        r = dev.read_bytes(1)[0]

    if do_print:
        print(f"{format(r, '#04x')}\t\t{interp_byte(r)}{note}")
    if interp_byte(r) != "ACK":
        raise RuntimeError(f"Did not receive ACK, but {hex(r)}.")

def uart_init(dev):
    dev.write_raw(struct.pack("B", 0x7F))
    get_ack(dev, note="")


def download(dev, num, data, do_print=False):
    # Data sanity check
    if do_print:
        print(f"Packet number {num} of length {len(data)}:")
    if len(data) > 256:
        raise RuntimeError("Too much data to send.")

    # Send "Download" command
    dev.write_raw(pack_cmd(0x31))

    # Response: ACK
    get_ack(dev, " command")

    # Packet number
    i0 = (num >> 0*8) & 0xff
    i1 = (num >> 1*8) & 0xff
    i2 = (num >> 2*8) & 0xff
    dev.write_raw(struct.pack("BBBB", 0x00, i2, i1, i0))

    # Checksum byte: XOR (byte 3 to byte 6)
    dev.write_raw(struct.pack("B", i2 ^ i1 ^ i0))

    # Response: ACK
    get_ack(dev, " packet number")

    # Packet size (0 < N < 255)
    dev.write_raw(struct.pack("B", len(data) - 1))

    # N-1 data bytes
    for d in data:
        dev.write_raw(struct.pack("B", d))

    # Checksum byte: XOR (byte 8 to Last-1)
    checksum = len(data) - 1
    for d in data:
        checksum ^= d
    dev.write_raw(struct.pack("B", checksum))

    # Response: ACK
    get_ack(dev, " data")


def start(dev, addr):
    # Send "Start" command
    dev.write_raw(pack_cmd(0x21))

    # Response: ACK
    get_ack(dev, " command")

    # Start address
    i0 = (addr >> 0*8) & 0xff
    i1 = (addr >> 1*8) & 0xff
    i2 = (addr >> 2*8) & 0xff
    i3 = (addr >> 3*8) & 0xff
    dev.write_raw(struct.pack("BBBB", i3, i2, i1, i0))

    # Checksum byte: XOR (byte 3 to byte 6)
    dev.write_raw(struct.pack("B", i3 ^ i2 ^ i1 ^ i0))

    # Response: ACK
    get_ack(dev, " address")


def down_file(dev, fname='tf-a-stm32mp135f-dk.stm32'):
    # size of each chunk (must be <= 256 bytes)
    sz = 256

    # open file with the bitstream
    with open(fname, 'rb') as f:
        fb = f.read()

    # split file into this many chunks
    num_chunks = int(math.ceil(len(fb) / sz))

    # send each chunk one by one
    for i in range(num_chunks):
        chunk = fb[i*sz : (i+1)*sz]
        download(dev, i, chunk)
        print('.', end='', flush=True)
    print()

    # necessary to finalize download
    start(dev, 0xFFFFFFFF)


def main():
    parser = argparse.ArgumentParser(description="Send executable to boot ROM")
    parser.add_argument('-c', '--com_port', help='COM port', required=True)
    parser.add_argument('-f', '--stm32_file', help='binary file', required=True)
    args = parser.parse_args()

    rm = pyvisa.ResourceManager()

    for i in range(3):
        try:
            with rm.open_resource(args.com_port) as mp1:
                pass
        except pyvisa.errors.VisaIOError:
            pass

    with rm.open_resource(args.com_port) as mp1:
        mp1.baud_rate  = 115200
        mp1.parity     = pyvisa.constants.Parity.even
        mp1.stop_bits  = pyvisa.constants.StopBits.one
        mp1.timeout    = 500

        # clear the read buffer
        try:
            while True:
                mp1.read()
        except pyvisa.errors.VisaIOError:
            pass

        # download the file
        uart_init(mp1)
        down_file(mp1, fname=args.stm32_file)


if __name__ == '__main__':
    main()
