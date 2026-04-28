# SPDX-License-Identifier: BSD-3-Clause
# verify.py --- Per-check dispatcher for baremetal/uart_echo automated test.
# Copyright (c) 2026 Stanford Research Systems, Inc.

import os
import sys

UART   = "streams/mp135.uart.bin"
ERRORS = "errors.log"


def _uart():
    with open(UART, "rb") as f:
        return f.read()


def _expect(needle, label):
    raw = _uart()
    if needle in raw:
        sys.stdout.write(f"matched {label}\n")
        return True
    sys.stderr.write(f"missing {label}\n")
    sys.stderr.write("--- last 512 bytes of UART ---\n")
    sys.stderr.write(raw[-512:].decode("ascii", "replace"))
    sys.stderr.write("\n")
    return False


def check_no_errors():
    if not os.path.isfile(ERRORS):
        sys.stdout.write("no errors.log\n")
        return True
    size = os.path.getsize(ERRORS)
    if size == 0:
        sys.stdout.write("errors.log empty\n")
        return True
    with open(ERRORS, "r") as f:
        sys.stderr.write(f.read())
    return False


def check_prompt():
    return _expect(b"> ", "prompt")


def check_echo():
    return _expect(b"abc", "echoed abc")


def check_unknown():
    return _expect(b"Unknown command", "Unknown command response")


def check_ddr():
    return _expect(b"SUCCESS: Byte 0 worked", "ddrtest SUCCESS")


DISPATCH = {
    "Check `test_serv` had no errors":                       check_no_errors,
    "Check the `> ` prompt appeared after reset":            check_prompt,
    "Check typed characters echoed back to the host":        check_echo,
    "Check unknown-command line produced `Unknown command '...'`":
                                                             check_unknown,
    "Check `ddrtest` reported SUCCESS at `0xC0001000`":      check_ddr,
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
