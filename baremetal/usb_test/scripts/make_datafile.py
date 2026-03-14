#!/usr/bin/env python3
"""
Write raw bytes to a file where each byte is the numbers 0..255.

Usage:
    python3 write_bytes.py out.bin          # writes 0..255 once to out.bin
    python3 write_bytes.py out.bin -r 10   # writes 0..255 repeated 10 times
"""

import argparse
from pathlib import Path

def main():
    p = argparse.ArgumentParser(description="Write raw bytes 0..255 to a file.")
    p.add_argument("outfile", help="output filename (binary)")
    p.add_argument("-r", "--repeats", type=int, default=1,
                   help="how many times to repeat the 0..255 sequence (default: 1)")
    args = p.parse_args()

    if args.repeats < 1:
        raise SystemExit("repeats must be >= 1")

    outpath = Path(args.outfile)
    sequence = bytes(range(256))  # bytes object containing values 0..255

    # Write in one go (efficient). For very large repeats this still uses memory
    # proportional to 256 * repeats; if repeats is huge, consider streaming in chunks.
    with outpath.open("wb") as f:
        f.write(sequence * args.repeats)

    print(f"Wrote {256 * args.repeats} bytes to '{outpath}'")

if __name__ == "__main__":
    main()
