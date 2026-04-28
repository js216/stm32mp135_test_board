# SPDX-License-Identifier: BSD-3-Clause
# verify.py --- Per-check dispatcher for baremetal/qspi automated test.
# Copyright (c) 2026 Stanford Research Systems, Inc.

import json
import os
import re
import sys

UART = "streams/mp135.uart.bin"
SENTINEL = "sentinel.txt"
ERRORS = "errors.log"

JEDEC_RE = re.compile(rb"JEDEC ID: [0-9a-f]{2} [0-9a-f]{2} [0-9a-f]{2}"
                      rb"\s+\((\d+) reads\)")


def _read_uart_raw():
    with open(UART, "rb") as f:
        return f.read()


def _has(pattern):
    """Substring or regex match (bytes) anywhere in the UART stream."""
    raw = _read_uart_raw()
    if isinstance(pattern, bytes):
        return pattern in raw
    return pattern.search(raw) is not None


def _expect(pattern, label):
    if _has(pattern):
        sys.stdout.write(f"matched {label}\n")
        return True
    sys.stderr.write(f"missing {label}\n")
    sys.stderr.write(f"--- last 1024 bytes of UART ---\n")
    sys.stderr.write(_read_uart_raw()[-1024:].decode("ascii", "replace"))
    sys.stderr.write("\n")
    return False


def check_no_errors():
    if os.path.isfile(ERRORS):
        sys.stderr.write("errors.log present:\n")
        with open(ERRORS) as f:
            sys.stderr.write(f.read())
        return False
    if not os.path.isfile(SENTINEL):
        sys.stderr.write("sentinel.txt missing\n")
        return False
    with open(SENTINEL) as f:
        manifest = json.load(f)
    n = manifest.get("n_errors")
    if n != 0:
        sys.stderr.write(f"sentinel reports n_errors={n}\n")
        return False
    return True


def check_loop():
    matches = JEDEC_RE.findall(_read_uart_raw())
    sys.stdout.write(f"{len(matches)} JEDEC lines\n")
    if len(matches) < 3:
        sys.stderr.write(
            f"only {len(matches)} JEDEC lines (need >= 3)\n")
        return False
    return True


def check_throughput():
    matches = JEDEC_RE.findall(_read_uart_raw())
    if len(matches) < 2:
        sys.stderr.write("need at least 2 JEDEC lines to measure rate\n")
        return False
    counts = [int(m) for m in matches]
    rates = [counts[i + 1] - counts[i] for i in range(len(counts) - 1)]
    avg = sum(rates) / len(rates)
    sys.stdout.write(f"avg {avg:.0f} reads/sec\n")
    if avg < 1000:
        sys.stderr.write(f"{avg:.0f} reads/sec below 1000\n")
        return False
    return True


# --- per-command response checks --------------------------------------

JEDEC_PLAIN = re.compile(rb"JEDEC ID: [0-9a-f]{2} [0-9a-f]{2} [0-9a-f]{2}")
HEXLINE = re.compile(rb"[0-9a-f]{8}:(?:\s+[0-9a-f]{2}){1,16}")


def check_help():
    raw = _read_uart_raw()
    # Look for a few of the help description fragments.
    needles = [b"JEDEC RDID", b"SFDP read", b"opcode-only", b"chip erase",
               b"this help"]
    missing = [n for n in needles if n not in raw]
    if missing:
        sys.stderr.write(f"help missing: {missing}\n")
        return False
    sys.stdout.write("help OK\n")
    return True


def check_jedec_response():
    return _expect(JEDEC_PLAIN, "JEDEC ID line")


def check_busy_status():
    return _expect(re.compile(rb"busy=\d+ presc=\d+ fsize=\d+ csht=\d+"),
                   "busy/presc/fsize/csht")


def check_status_read():
    return _expect(re.compile(rb"op=05 -> [0-9a-f]{2}"), "op=05 -> XX")


def check_wren_response():
    return _expect(b"op=06 sent", "op=06 sent")


def check_read_response():
    raw = _read_uart_raw()
    if b"op=03 read 16 @ 0x00000000" not in raw:
        sys.stderr.write("missing op=03 read header\n")
        return False
    if not HEXLINE.search(raw):
        sys.stderr.write("no hex-dump lines\n")
        return False
    return True


def check_fast_read_response():
    return _expect(b"op=0b read 16 @ 0x00000000", "op=0b read header")


def check_quad_read_response():
    return _expect(b"op=6b read 16 @ 0x00000000", "op=6b read header")


def check_quad_io_read_response():
    return _expect(b"op=eb read 16 @ 0x00000000", "op=eb read header")


def check_write_response():
    return _expect(b"op=02 wrote 16 bytes @ 0x00000000",
                   "op=02 wrote header")


def check_quad_write_response():
    return _expect(b"op=32 wrote 16 bytes @ 0x00000000",
                   "op=32 wrote header")


def check_sector_erase_response():
    return _expect(b"op=20 erased 4 KB @ 0x00000000",
                   "op=20 erased header")


def check_sfdp_response():
    raw = _read_uart_raw()
    if b"SFDP @ 0x00000000 (16 bytes)" not in raw:
        sys.stderr.write("missing SFDP header\n")
        return False
    if not HEXLINE.search(raw):
        sys.stderr.write("no hex-dump after SFDP\n")
        return False
    return True


def check_mm_response():
    return _expect(b"MM read 16 @ 0x00000000", "MM read header")


def check_bench_response():
    return _expect(re.compile(rb"bench \d+ B (?:1lane|quad) in \d+ ms"),
                   "bench summary line")


def check_bench_1lane_response():
    return _expect(re.compile(rb"bench \d+ B 1lane in \d+ ms"),
                   "bench 1lane summary line")


def check_bench_quad_response():
    return _expect(re.compile(rb"bench \d+ B quad in \d+ ms"),
                   "bench quad summary line")


def check_block_erase_response():
    return _expect(b"op=d8 erased 64 KB @ 0x00010000",
                   "op=d8 erased header")


def check_wrsr_response():
    return _expect(re.compile(rb"op=01 wrote SR=[0-9a-f]{2}"),
                   "op=01 wrote SR=XX")


def check_dual_read_response():
    return _expect(b"op=3b read 16 @ 0x00000000", "op=3b read header")


def check_dual_io_read_response():
    return _expect(b"op=bb read 16 @ 0x00000000", "op=bb read header")


def check_dpd_response():
    return _expect(b"op=b9 deep power-down", "op=b9 line")


def check_release_dpd_response():
    return _expect(b"op=ab release DPD", "op=ab line")


def check_reset_response():
    return _expect(b"reset (66 99) sent", "reset 66 99 line")


def check_4byte_enter_response():
    return _expect(b"4byte=1 (op=b7)", "4byte=1 line")


def check_4byte_exit_response():
    return _expect(b"4byte=0 (op=e9)", "4byte=0 line")


def check_gpio_high_response():
    return _expect(re.compile(rb"gpio mask=0x3f: CLK=\d NCS=\d IO0=\d "
                              rb"IO1=\d IO2=\d IO3=\d"),
                   "gpio mask=0x3f line")


def check_gpio_low_response():
    return _expect(re.compile(rb"gpio mask=0x00: CLK=\d NCS=\d IO0=\d "
                              rb"IO1=\d IO2=\d IO3=\d"),
                   "gpio mask=0x00 line")


def check_autopoll_response():
    return _expect(
        re.compile(rb"autopoll op=05 mask=01 match=00 done in \d+ ms"),
        "autopoll success line")


def check_autopoll_timeout_response():
    return _expect(re.compile(rb"autopoll TIMEOUT after \d+ ms"),
                   "autopoll TIMEOUT line")


def check_chip_erase_confirms():
    return _expect(b"Type C again to confirm.",
                   "chip erase confirm prompt")


def check_chip_erase_executed():
    return _expect(b"op=c7 erased chip", "op=c7 erased chip line")


DISPATCH = {
    "Check `test_serv` had no errors": check_no_errors,
    "Check JEDEC ID line repeats (loop runs at least 3 times)": check_loop,
    "Check QSPI throughput is at least 1000 reads/sec": check_throughput,
    "Check help output present": check_help,
    "Check JEDEC response pattern": check_jedec_response,
    "Check busy status query": check_busy_status,
    "Check status read": check_status_read,
    "Check WREN op response": check_wren_response,
    "Check 1-lane read response": check_read_response,
    "Check fast read response": check_fast_read_response,
    "Check quad-output read response": check_quad_read_response,
    "Check quad-IO read response": check_quad_io_read_response,
    "Check write op response": check_write_response,
    "Check quad write op response": check_quad_write_response,
    "Check sector erase response": check_sector_erase_response,
    "Check SFDP response": check_sfdp_response,
    "Check memory-mapped response": check_mm_response,
    "Check bench output present": check_bench_response,
    "Check bench 1-lane output present": check_bench_1lane_response,
    "Check bench quad output present": check_bench_quad_response,
    "Check block erase response": check_block_erase_response,
    "Check WRSR response": check_wrsr_response,
    "Check dual-output read response": check_dual_read_response,
    "Check dual-IO read response": check_dual_io_read_response,
    "Check autopoll timeout response": check_autopoll_timeout_response,
    "Check chip erase executed": check_chip_erase_executed,
    "Check DPD response": check_dpd_response,
    "Check release DPD response": check_release_dpd_response,
    "Check soft reset response": check_reset_response,
    "Check 4-byte enter response": check_4byte_enter_response,
    "Check 4-byte exit response": check_4byte_exit_response,
    "Check gpio set high response": check_gpio_high_response,
    "Check gpio set low response": check_gpio_low_response,
    "Check autopoll response": check_autopoll_response,
    "Check chip erase confirms": check_chip_erase_confirms,
}


def main(argv):
    if len(argv) != 2:
        sys.stderr.write("usage: verify.py <key>\n")
        return 1
    key = argv[1].strip()
    fn = DISPATCH.get(key)
    if fn is None:
        sys.stderr.write(f"unknown check: {key!r}\n")
        return 1
    return 0 if fn() else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
