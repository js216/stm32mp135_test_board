# SPDX-License-Identifier: BSD-3-Clause
# verify.py --- Per-check dispatcher for baremetal/qspi automated test.
# Copyright (c) 2026 Stanford Research Systems, Inc.
#
# Each check parses the captured UART stream and asserts that the
# QSPI slave actually behaved like a real NOR flash, not just that the
# host emitted the right CLI command.

import json
import os
import re
import sys

UART = "streams/mp135.uart.bin"
FPGA_UART = "streams/fpga.uart.bin"
SENTINEL = "sentinel.txt"
ERRORS = "errors.log"

JEDEC_RE = re.compile(rb"JEDEC ID: ([0-9a-f]{2}) ([0-9a-f]{2}) ([0-9a-f]{2})"
                      rb"(?:\s+\((\d+) reads\))?")

# m25p80 / spi-nor bank-1 manufacturer IDs (subset, JEDEC bank 1).
SPI_NOR_MFR_IDS = {
    0x01,  # Spansion / Cypress / Infineon
    0x1C,  # eON
    0x1F,  # Atmel / Adesto
    0x20,  # Micron / Numonyx / ST
    0x37,  # AMIC
    0x4A,  # Everspin
    0x62,  # SST (legacy)
    0x89,  # Intel
    0x9D,  # ISSI / PMC
    0xBF,  # SST
    0xC2,  # Macronix
    0xC8,  # GigaDevice
    0xEF,  # Winbond
}


def _read_uart_raw():
    with open(UART, "rb") as f:
        return f.read()


def _has(pattern):
    raw = _read_uart_raw()
    if isinstance(pattern, bytes):
        return pattern in raw
    return pattern.search(raw) is not None


def _expect(pattern, label):
    if _has(pattern):
        sys.stdout.write(f"matched {label}\n")
        return True
    sys.stderr.write(f"missing {label}\n")
    sys.stderr.write("--- last 1024 bytes of UART ---\n")
    sys.stderr.write(_read_uart_raw()[-1024:].decode("ascii", "replace"))
    sys.stderr.write("\n")
    return False


# --- helpers ----------------------------------------------------------

HEXLINE = re.compile(rb"[0-9a-f]{8}:(?:\s+[0-9a-f]{2}){1,16}")
HEX_TOK = re.compile(rb"\b([0-9a-f]{2})\b")


def _bytes_after(header_re, n):
    """Find header_re in the UART stream, return list of N hex byte ints
    that follow on subsequent CLI hex-dump lines.  None if not found or
    fewer than N bytes parsed before a non-hex marker."""
    raw = _read_uart_raw()
    if isinstance(header_re, (bytes, str)):
        m = re.search(re.escape(header_re if isinstance(header_re, bytes)
                                else header_re.encode()), raw)
    else:
        m = header_re.search(raw)
    if not m:
        return None
    tail = raw[m.end():]
    # Stop at the next prompt line ("> ") or another response header.
    # Capture only the run of "addr: HH HH ..." lines.
    out = []
    for line in tail.splitlines():
        s = line.strip()
        if not s:
            continue
        if not HEXLINE.match(line):
            # First non-hex-dump line ends the block.
            break
        # Skip the leading "addr:" token, take HH bytes only.
        parts = s.split(b":", 1)
        if len(parts) != 2:
            break
        for tok in HEX_TOK.finditer(parts[1]):
            out.append(int(tok.group(1), 16))
            if len(out) >= n:
                return out
    if len(out) >= n:
        return out[:n]
    return None


def _last_jedec():
    """Return tuple (b0, b1, b2) of the most recent JEDEC ID line, or
    None if none found."""
    raw = _read_uart_raw()
    last = None
    for m in JEDEC_RE.finditer(raw):
        last = (int(m.group(1), 16), int(m.group(2), 16),
                int(m.group(3), 16))
    return last


def _all_jedecs():
    raw = _read_uart_raw()
    out = []
    for m in JEDEC_RE.finditer(raw):
        out.append((int(m.group(1), 16), int(m.group(2), 16),
                    int(m.group(3), 16)))
    return out


def _status_after(cmd_marker, opc=0x05):
    """Find an `op=XX -> YY` line that follows the literal cmd_marker
    text (e.g. b'\\r\\n> e 0x06\\r\\n').  Returns YY int or None."""
    raw = _read_uart_raw()
    pos = raw.find(cmd_marker)
    if pos < 0:
        return None
    rest = raw[pos + len(cmd_marker):]
    pat = re.compile(rb"op=%02x -> ([0-9a-f]{2})" % opc)
    m = pat.search(rest)
    if not m:
        return None
    return int(m.group(1), 16)


def _all_status_reads(opc=0x05):
    raw = _read_uart_raw()
    pat = re.compile(rb"op=%02x -> ([0-9a-f]{2})" % opc)
    return [int(m.group(1), 16) for m in pat.finditer(raw)]


def _read_block(addr, n, op=0x03):
    header = ("op=%02x read %d @ 0x%08x" % (op, n, addr)).encode()
    return _bytes_after(re.compile(re.escape(header)), n)


def _read_block_at(header_str, n):
    return _bytes_after(re.compile(re.escape(header_str.encode())), n)


def _mm_read(addr, n):
    header = ("MM read %d @ 0x%08x" % (n, addr)).encode()
    return _bytes_after(re.compile(re.escape(header)), n)


def _sfdp_read(addr, n):
    header = ("SFDP @ 0x%08x (%d bytes)" % (addr, n)).encode()
    return _bytes_after(re.compile(re.escape(header)), n)


def _autopoll_ms(start_re=None):
    """Return the LAST autopoll completion time in ms (None on miss)."""
    raw = _read_uart_raw()
    pat = re.compile(rb"autopoll op=[0-9a-f]{2} mask=[0-9a-f]{2} "
                     rb"match=[0-9a-f]{2} done in (\d+) ms")
    matches = pat.findall(raw)
    if not matches:
        return None
    return int(matches[-1])


def _autopoll_ms_after(marker):
    """Return the FIRST autopoll completion time after marker text."""
    raw = _read_uart_raw()
    p = raw.find(marker)
    if p < 0:
        return None
    pat = re.compile(rb"autopoll op=[0-9a-f]{2} mask=[0-9a-f]{2} "
                     rb"match=[0-9a-f]{2} done in (\d+) ms")
    m = pat.search(raw, p)
    if not m:
        return None
    return int(m.group(1))


# --- baseline checks --------------------------------------------------

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
    raw = _read_uart_raw()
    looped = re.findall(rb"JEDEC ID: [0-9a-f]{2} [0-9a-f]{2} [0-9a-f]{2}"
                        rb"\s+\((\d+) reads\)", raw)
    sys.stdout.write(f"{len(looped)} JEDEC loop lines\n")
    if len(looped) < 3:
        sys.stderr.write(f"only {len(looped)} loop lines (need >= 3)\n")
        return False
    return True


def check_throughput():
    raw = _read_uart_raw()
    counts = [int(m) for m in re.findall(
        rb"JEDEC ID: [0-9a-f]{2} [0-9a-f]{2} [0-9a-f]{2}"
        rb"\s+\((\d+) reads\)", raw)]
    if len(counts) < 2:
        sys.stderr.write("need at least 2 loop lines\n")
        return False
    rates = [counts[i + 1] - counts[i] for i in range(len(counts) - 1)]
    avg = sum(rates) / len(rates)
    sys.stdout.write(f"avg {avg:.0f} reads/sec\n")
    if avg < 1000:
        sys.stderr.write(f"{avg:.0f} reads/sec below 1000\n")
        return False
    return True


# --- 1 MB throughput bench (Block 2) ---------------------------------

_BENCH_RE = re.compile(
    rb"bench (\d+) B (1lane|quad)(?: @ presc=(\d+))? in (\d+) ms,"
    rb"\s+(\d+) KB/s,\s+crc32=([0-9a-f]+),\s+firsterr=(-?\d+)"
    rb"(?:\s+\(exp=([0-9a-f]+) got=([0-9a-f]+)\))?")


def _bench_lines():
    out = []
    for m in _BENCH_RE.finditer(_read_uart_raw()):
        out.append({
            "len":      int(m.group(1)),
            "mode":     m.group(2).decode(),
            "presc":    int(m.group(3)) if m.group(3) else None,
            "ms":       int(m.group(4)),
            "kbps":     int(m.group(5)),
            "crc":      int(m.group(6), 16),
            "firsterr": int(m.group(7)),
            "fe_exp":   int(m.group(8), 16) if m.group(8) else None,
            "fe_got":   int(m.group(9), 16) if m.group(9) else None,
        })
    return out


def _bench_pick(length, mode):
    for b in _bench_lines():
        if b["len"] == length and b["mode"] == mode:
            return b
    return None


_PRESC_STEPS = [203, 63, 15, 5, 2, 1]
_PRESC_MHZ   = {203: 1.63, 63: 5.19, 15: 20.75, 5: 55.33, 2: 110.67, 1: 166.0}


def _bench_picks_by_presc(length, mode):
    """Return list of (presc, bench-dict) for every bench line of the
    given length and mode that has a presc tag.  In step order."""
    return [(b["presc"], b) for b in _bench_lines()
            if b["len"] == length and b["mode"] == mode
            and b["presc"] is not None]


def _scan_runs_by_presc(mode):
    """Return dict {presc: bench_dict} for the given mode."""
    return {p: b for p, b in _bench_picks_by_presc(1048576, mode)}


def _format_bench(b):
    fe = b["firsterr"]
    if fe < 0:
        return (f"{b['ms']} ms, {b['kbps']} KB/s, firsterr=-1")
    if b["fe_exp"] is not None and b["fe_got"] is not None:
        return (f"{b['ms']} ms, {b['kbps']} KB/s, firsterr={fe} "
                f"(exp={b['fe_exp']:02x} got={b['fe_got']:02x})")
    return (f"{b['ms']} ms, {b['kbps']} KB/s, firsterr={fe}")


def _scan_count_check(mode):
    runs = _scan_runs_by_presc(mode)
    sys.stdout.write(
        f"{mode} benches captured: {len(runs)}/{len(_PRESC_STEPS)}\n")
    for p in _PRESC_STEPS:
        if p in runs:
            sys.stdout.write(
                f"  presc={p:3d} ({_PRESC_MHZ[p]:6.2f} MHz): "
                f"{_format_bench(runs[p])}\n")
        else:
            sys.stdout.write(
                f"  presc={p:3d} ({_PRESC_MHZ[p]:6.2f} MHz): MISSING\n")
    return all(p in runs for p in _PRESC_STEPS)


def check_1mb_scan_1lane_count():
    return _scan_count_check("1lane")


def check_1mb_scan_quad_count():
    return _scan_count_check("quad")


def check_1mb_scan_all_incrementing():
    one = _scan_runs_by_presc("1lane")
    quad = _scan_runs_by_presc("quad")
    missing = [p for p in _PRESC_STEPS if p not in one or p not in quad]
    if missing:
        sys.stdout.write(f"missing benches at presc={missing}\n")
        return False
    bad = []
    for p in _PRESC_STEPS:
        if one[p]["firsterr"] != -1:
            bad.append(("1lane", p, one[p]))
        if quad[p]["firsterr"] != -1:
            bad.append(("quad", p, quad[p]))
    if bad:
        sys.stdout.write("non-incrementing reads:\n")
        for mode, p, b in bad:
            sys.stdout.write(f"  {mode} presc={p:3d}: {_format_bench(b)}\n")
        return False
    sys.stdout.write(
        f"all {2 * len(_PRESC_STEPS)} benches match incrementing pattern\n")
    return True


def check_1mb_scan_quad_faster():
    one = _scan_runs_by_presc("1lane")
    quad = _scan_runs_by_presc("quad")
    missing = [p for p in _PRESC_STEPS if p not in one or p not in quad]
    if missing:
        sys.stdout.write(f"missing benches at presc={missing}\n")
        return False
    bad = []
    for p in _PRESC_STEPS:
        if quad[p]["kbps"] <= one[p]["kbps"]:
            bad.append((p, one[p]["kbps"], quad[p]["kbps"]))
    if bad:
        sys.stdout.write("quad not faster than 1lane:\n")
        for p, o, q in bad:
            sys.stdout.write(f"  presc={p:3d}: 1lane={o} KB/s quad={q} KB/s\n")
        return False
    speedups = [quad[p]["kbps"] / max(1, one[p]["kbps"])
                for p in _PRESC_STEPS]
    sys.stdout.write(
        "quad faster at every step (speedups: "
        + ", ".join(f"x{s:.2f}" for s in speedups) + ")\n")
    return True


def check_1mb_scan_max_quad_rate():
    quad = _scan_runs_by_presc("quad")
    if not quad:
        sys.stderr.write("no quad bench lines\n")
        return False
    best_p = min(quad.keys(), key=lambda p: -quad[p]["kbps"])
    best = quad[best_p]
    sys.stdout.write(
        f"max quad rate: {best['kbps']} KB/s ({best['kbps']/1024:.2f} MB/s)"
        f" at presc={best_p} ({_PRESC_MHZ.get(best_p, 0):.2f} MHz)\n")
    return best["kbps"] > 0


# --- JEDEC plausibility ----------------------------------------------

def check_jedec_plausible():
    j = _last_jedec()
    if j is None:
        sys.stderr.write("no JEDEC line\n")
        return False
    sys.stdout.write(f"JEDEC = {j[0]:02x} {j[1]:02x} {j[2]:02x}\n")
    if j == (0x00, 0x00, 0x00) or j == (0xFF, 0xFF, 0xFF):
        sys.stderr.write("JEDEC is all-zero or all-ff\n")
        return False
    if j[0] == 0xFF:
        sys.stderr.write("manufacturer 0xFF implausible\n")
        return False
    if j[2] == 0:
        sys.stderr.write("capacity exponent 0\n")
        return False
    return True


def check_jedec_mfr_in_list():
    j = _last_jedec()
    if j is None:
        sys.stderr.write("no JEDEC line\n")
        return False
    if j[0] not in SPI_NOR_MFR_IDS:
        sys.stderr.write(
            f"manufacturer 0x{j[0]:02x} not in m25p80 bank-1 list\n")
        return False
    sys.stdout.write(f"manufacturer 0x{j[0]:02x} in m25p80 list\n")
    return True


def check_jedec_capacity_128k():
    j = _last_jedec()
    if j is None:
        sys.stderr.write("no JEDEC line\n")
        return False
    size = 1 << j[2]
    sys.stdout.write(f"capacity exponent {j[2]} -> {size} B\n")
    if j[2] < 17:
        sys.stderr.write(f"size {size} B < 128 KB\n")
        return False
    return True


# --- SFDP parsing -----------------------------------------------------

def _sfdp_chunk(addr):
    """Return up to 256 bytes captured from `f <addr> 256`."""
    return _sfdp_read(addr, 256)


def _sfdp_byte(off):
    """Return a single SFDP byte at absolute offset `off` based on the
    captured 256-byte chunks at 0x000/0x100/0x200/0x300."""
    base = (off // 256) * 256
    rel = off % 256
    chunk = _sfdp_chunk(base)
    if chunk is None or rel >= len(chunk):
        return None
    return chunk[rel]


def _sfdp_bytes(off, n):
    out = []
    for i in range(n):
        b = _sfdp_byte(off + i)
        if b is None:
            return None
        out.append(b)
    return out


def check_sfdp_signature():
    sig = _sfdp_bytes(0, 4)
    if sig is None:
        sys.stderr.write("no SFDP @0 capture\n")
        return False
    sys.stdout.write(
        f"SFDP[0..4] = {' '.join(f'{b:02x}' for b in sig)}\n")
    if sig != [0x53, 0x46, 0x44, 0x50]:
        sys.stderr.write("SFDP signature mismatch\n")
        return False
    return True


def check_sfdp_first_param_bfpt():
    """First parameter header at offset 8: id-lsb at offset 8 must be
    0x00 (BFPT)."""
    ph = _sfdp_bytes(8, 8)
    if ph is None:
        sys.stderr.write("no SFDP param header capture\n")
        return False
    sys.stdout.write(
        f"SFDP param header = {' '.join(f'{b:02x}' for b in ph)}\n")
    if ph[0] != 0x00:
        sys.stderr.write(f"first param id-lsb 0x{ph[0]:02x} != 0x00\n")
        return False
    return True


def _bfpt_pointer():
    ph = _sfdp_bytes(8, 8)
    if ph is None:
        return None, None
    # Param header: id-lsb [0], minor [1], major [2], length-dw [3],
    #               PTP[3] [4], [5], [6], id-msb [7].
    length_dw = ph[3]
    ptp = ph[4] | (ph[5] << 8) | (ph[6] << 16)
    return ptp, length_dw


def check_sfdp_bfpt_4k_erase():
    """BFPT DWORD 1 (offset 0..3 within BFPT): bits [9:8] indicate 4 KB
    erase support; byte at BFPT+1 = 0x20 erase opcode if supported."""
    ptp, ldw = _bfpt_pointer()
    if ptp is None:
        sys.stderr.write("no BFPT pointer\n")
        return False
    dw1 = _sfdp_bytes(ptp, 4)
    if dw1 is None:
        sys.stderr.write(f"BFPT @ 0x{ptp:x} not captured\n")
        return False
    word = dw1[0] | (dw1[1] << 8) | (dw1[2] << 16) | (dw1[3] << 24)
    erase_opc = dw1[1]  # JESD216 BFPT-1[15:8]
    sys.stdout.write(f"BFPT DW1 = 0x{word:08x}, 4KB-erase-opc=0x{erase_opc:02x}\n")
    if erase_opc != 0x20:
        sys.stderr.write(
            f"BFPT 4K erase opcode 0x{erase_opc:02x} != 0x20\n")
        return False
    # bits [1:0] of DW1 should be 01 or 11 indicating 4KB granularity supported
    if (word & 0x3) == 0x0:
        sys.stderr.write("BFPT DW1 erase-size field indicates no 4 KB\n")
        return False
    return True


def check_sfdp_bfpt_density():
    """BFPT DWORD 2: density field.  If bit31=0, density = N+1 bits
    (so size_bytes = (N+1)/8).  If bit31=1, density = 1 << N bits."""
    ptp, ldw = _bfpt_pointer()
    if ptp is None:
        sys.stderr.write("no BFPT pointer\n")
        return False
    dw2 = _sfdp_bytes(ptp + 4, 4)
    if dw2 is None:
        sys.stderr.write(f"BFPT DW2 @ 0x{ptp+4:x} not captured\n")
        return False
    word = dw2[0] | (dw2[1] << 8) | (dw2[2] << 16) | (dw2[3] << 24)
    if word & (1 << 31):
        bits = 1 << (word & 0x7FFFFFFF)
    else:
        bits = (word & 0x7FFFFFFF) + 1
    sfdp_bytes = bits // 8
    sys.stdout.write(
        f"BFPT DW2 = 0x{word:08x} -> {sfdp_bytes} B\n")
    j = _last_jedec()
    if j is None:
        sys.stderr.write("no JEDEC for cross-check\n")
        return False
    rdid_bytes = 1 << j[2]
    if sfdp_bytes != rdid_bytes:
        sys.stderr.write(
            f"SFDP density {sfdp_bytes} B != RDID capacity "
            f"{rdid_bytes} B\n")
        return False
    return True


def check_sfdp_bfpt_qer():
    """BFPT DWORD 15 (1-indexed) = byte offset 14*4 = 56.  Bits 22..20
    are the QER field.  Sane = value in {0..6}."""
    ptp, ldw = _bfpt_pointer()
    if ptp is None:
        sys.stderr.write("no BFPT pointer\n")
        return False
    if ldw is not None and ldw < 15:
        sys.stderr.write(f"BFPT length {ldw} dw too short for QER\n")
        return False
    dw = _sfdp_bytes(ptp + 14 * 4, 4)
    if dw is None:
        sys.stderr.write(f"BFPT DW15 @ 0x{ptp+56:x} not captured\n")
        return False
    word = dw[0] | (dw[1] << 8) | (dw[2] << 16) | (dw[3] << 24)
    qer = (word >> 20) & 0x7
    sys.stdout.write(f"BFPT DW15 = 0x{word:08x}, QER={qer}\n")
    if qer > 6:
        sys.stderr.write(f"QER {qer} not in 0..6\n")
        return False
    return True


# --- WEL/WIP/WRSR -----------------------------------------------------

def check_wel_lifecycle():
    """Sequence: s, e 0x06, s, e 0x04, s. Expect SR sequence with
    WEL=0, WEL=1, WEL=0 across the three reads."""
    vals = _all_status_reads(0x05)
    if len(vals) < 3:
        sys.stderr.write(f"need 3 status reads, got {len(vals)}\n")
        return False
    v0, v1, v2 = vals[0], vals[1], vals[2]
    sys.stdout.write(
        f"SR sequence: 0x{v0:02x} 0x{v1:02x} 0x{v2:02x}\n")
    if (v0 & 0x02) != 0:
        sys.stderr.write("initial WEL not 0\n")
        return False
    if (v1 & 0x02) == 0:
        sys.stderr.write("WEL did not set after WREN\n")
        return False
    if (v2 & 0x02) != 0:
        sys.stderr.write("WEL did not clear after WRDI\n")
        return False
    return True


def check_wip_during_erase():
    """In block 21 the sequence is `e 0x06 / S 0 / s 0x05 / P`.
    The single status read should show WIP=1."""
    vals = _all_status_reads(0x05)
    if not vals:
        sys.stderr.write("no status reads\n")
        return False
    sys.stdout.write(f"SR after S: 0x{vals[0]:02x}\n")
    if (vals[0] & 0x01) == 0:
        sys.stderr.write("WIP not 1 immediately after sector erase\n")
        return False
    return True


def check_autopoll_erase_min_ms():
    ms = _autopoll_ms()
    if ms is None:
        sys.stderr.write("no autopoll line\n")
        return False
    sys.stdout.write(f"autopoll done in {ms} ms\n")
    if ms < 10:
        sys.stderr.write(f"autopoll {ms} ms < 10 ms (chip not waiting)\n")
        return False
    return True


def check_autopoll_post_erase_nonzero():
    ms = _autopoll_ms()
    if ms is None:
        sys.stderr.write("no autopoll line\n")
        return False
    sys.stdout.write(f"autopoll done in {ms} ms\n")
    if ms <= 0:
        sys.stderr.write("autopoll completed in 0 ms (slave not tracking WIP)\n")
        return False
    return True


def check_autopoll_timeout():
    return _expect(re.compile(rb"autopoll TIMEOUT after \d+ ms"),
                   "autopoll TIMEOUT line")


def check_wrsr_not_persistent():
    """Block 22: e / W 0xBC / P / s 0x05 / R / s 0x05.
    Need 2 status reads. The post-reset value must NOT equal 0xBC."""
    vals = _all_status_reads(0x05)
    if len(vals) < 2:
        sys.stderr.write(f"need 2 SR reads, got {len(vals)}\n")
        return False
    pre, post = vals[0], vals[1]
    sys.stdout.write(f"SR pre-reset=0x{pre:02x} post-reset=0x{post:02x}\n")
    if post == 0xBC:
        sys.stderr.write("WRSR value persisted across soft reset\n")
        return False
    return True


def check_soft_reset_clears_wel():
    """Block 14: e 0x06 / R / s 0x05.  WEL must be 0."""
    vals = _all_status_reads(0x05)
    if not vals:
        sys.stderr.write("no SR read\n")
        return False
    v = vals[-1]
    sys.stdout.write(f"SR after reset: 0x{v:02x}\n")
    if (v & 0x02) != 0:
        sys.stderr.write("WEL still set after soft reset\n")
        return False
    return True


# --- erase / program correctness --------------------------------------

def _all_ff(buf):
    return buf is not None and all(b == 0xFF for b in buf)


def check_sector_erase_ff():
    buf = _read_block(0x0, 16)
    if buf is None:
        sys.stderr.write("no read at 0x0\n")
        return False
    sys.stdout.write(f"@0x0: {' '.join(f'{b:02x}' for b in buf)}\n")
    if not _all_ff(buf):
        sys.stderr.write("post-sector-erase region not all ff\n")
        return False
    return True


def check_block_erase_ff():
    buf = _read_block(0x10000, 16)
    if buf is None:
        sys.stderr.write("no read at 0x10000\n")
        return False
    sys.stdout.write(f"@0x10000: {' '.join(f'{b:02x}' for b in buf)}\n")
    if not _all_ff(buf):
        sys.stderr.write("post-block-erase region not all ff\n")
        return False
    return True


def check_pp_pattern():
    buf = _read_block(0x0, 16)
    if buf is None:
        sys.stderr.write("no read at 0x0\n")
        return False
    expect = [i & 0xFF for i in range(16)]
    sys.stdout.write(f"got: {' '.join(f'{b:02x}' for b in buf)}\n")
    if buf != expect:
        sys.stderr.write(f"PP read-back != i&0xff pattern\n")
        return False
    return True


def check_qpp_pattern():
    buf = _read_block(0x200, 16)
    if buf is None:
        sys.stderr.write("no read at 0x200\n")
        return False
    expect = [0xC0 + (i & 0x0F) for i in range(16)]
    sys.stdout.write(f"got: {' '.join(f'{b:02x}' for b in buf)}\n")
    if buf != expect:
        sys.stderr.write("QPP read-back != 0xc0+i&0xf pattern\n")
        return False
    return True


def _chunks(addr, n=16):
    return _read_block(addr, n)


def _compare_at(addr, op_header, n=16):
    a = _read_block(addr, n)
    b = _bytes_after(re.compile(re.escape(op_header.encode())), n)
    if a is None or b is None:
        sys.stderr.write(f"missing read: r={a is not None} alt={b is not None}\n")
        return False
    sys.stdout.write(
        f"r:   {' '.join(f'{x:02x}' for x in a)}\n"
        f"alt: {' '.join(f'{x:02x}' for x in b)}\n")
    if a != b:
        sys.stderr.write("data mismatch\n")
        return False
    return True


def check_quad_matches_1lane_at_100():
    return _compare_at(0x100, "op=6b read 16 @ 0x00000100")


def check_quad_io_matches_1lane_at_100():
    return _compare_at(0x100, "op=eb read 16 @ 0x00000100")


def check_dual_matches_1lane_at_400():
    return _compare_at(0x400, "op=3b read 16 @ 0x00000400")


def check_dual_io_matches_1lane_at_400():
    return _compare_at(0x400, "op=bb read 16 @ 0x00000400")


def check_fast_matches_1lane_at_500():
    return _compare_at(0x500, "op=0b read 16 @ 0x00000500")


def check_mm_matches_1lane_at_300():
    a = _read_block(0x300, 16)
    b = _mm_read(0x300, 16)
    if a is None or b is None:
        sys.stderr.write(f"missing: r={a is not None} mm={b is not None}\n")
        return False
    sys.stdout.write(
        f"r:  {' '.join(f'{x:02x}' for x in a)}\n"
        f"mm: {' '.join(f'{x:02x}' for x in b)}\n")
    if a != b:
        sys.stderr.write("MM != 1-lane mismatch\n")
        return False
    return True


def check_chip_erase_spot_0():
    buf = _read_block(0x0, 16)
    if buf is None:
        sys.stderr.write("no read at 0x0\n")
        return False
    if not _all_ff(buf):
        sys.stderr.write(
            f"post-chip-erase @0x0: {' '.join(f'{b:02x}' for b in buf)}\n")
        return False
    return True


def check_chip_erase_spot_10000():
    buf = _read_block(0x10000, 16)
    if buf is None:
        sys.stderr.write("no read at 0x10000\n")
        return False
    if not _all_ff(buf):
        sys.stderr.write(
            f"post-chip-erase @0x10000: {' '.join(f'{b:02x}' for b in buf)}\n")
        return False
    return True


def check_chip_erase_spot_100000():
    buf = _read_block(0x100000, 16)
    if buf is None:
        sys.stderr.write("no read at 0x100000\n")
        return False
    if not _all_ff(buf):
        sys.stderr.write(
            f"post-chip-erase @0x100000: {' '.join(f'{b:02x}' for b in buf)}\n")
        return False
    return True


# --- DPD / soft reset / page boundary / etc. -------------------------

def check_dpd_silences_then_release_restores():
    """Block 13 captures three JEDEC lines from `i`: pre-DPD, during
    DPD (must be 00 00 00 or ff ff ff), post-release (must equal pre).
    The infinite background loop also produces JEDEC lines; we have to
    use only the explicit response lines (no `(N reads)` suffix)."""
    raw = _read_uart_raw()
    # Match only "JEDEC ID: hh hh hh" not followed by "(N reads)".
    pat = re.compile(rb"JEDEC ID: ([0-9a-f]{2}) ([0-9a-f]{2}) ([0-9a-f]{2})"
                     rb"(?!\s*\()")
    explicit = [(int(m.group(1), 16), int(m.group(2), 16),
                 int(m.group(3), 16)) for m in pat.finditer(raw)]
    sys.stdout.write(f"explicit JEDECs: {explicit}\n")
    if len(explicit) < 3:
        sys.stderr.write(f"need 3 explicit JEDEC lines, got {len(explicit)}\n")
        return False
    pre, during, post = explicit[0], explicit[1], explicit[2]
    if during not in [(0x00, 0x00, 0x00), (0xFF, 0xFF, 0xFF)]:
        sys.stderr.write(
            f"DPD JEDEC {during} is not 00 00 00 or ff ff ff\n")
        return False
    if post != pre:
        sys.stderr.write(f"post-release JEDEC {post} != pre-DPD {pre}\n")
        return False
    return True


def check_pp_no_cross_page():
    """Block 18: pre-program 0x100..0x10F with QPP pattern (0xc0+i&0xf),
    write i&0xff at 0xF8 for 16 bytes, then read 0xF0..0x10F (32 B).
    Bytes 0xF0..0xF7 = 0xff (chip-erase state).
    Bytes 0xF8..0xFF = 0x00..0x07 (i&0xff for i=0..7).
    Bytes 0x100..0x10F = 0xc0..0xcf (untouched QPP pattern)."""
    buf = _bytes_after(
        re.compile(rb"op=03 read 32 @ 0x000000f0"), 32)
    if buf is None:
        sys.stderr.write("no 32 B read at 0xF0\n")
        return False
    sys.stdout.write(f"got: {' '.join(f'{b:02x}' for b in buf)}\n")
    # Bytes 0..7: should be 0xff (pre-erased and not touched)
    for i in range(8):
        if buf[i] != 0xFF:
            sys.stderr.write(f"byte 0xF{i:X} = 0x{buf[i]:02x} != 0xff\n")
            return False
    # Bytes 8..15: should be the i&0xff pattern from `w 0xF8 16` (i=0..7)
    for i in range(8):
        if buf[8 + i] != i:
            sys.stderr.write(
                f"byte 0x{0xF8+i:X} = 0x{buf[8+i]:02x} != 0x{i:02x}\n")
            return False
    # Bytes 16..31: must be the QPP pattern UNCHANGED (no wrap)
    expect = [0xC0 + (i & 0xF) for i in range(16)]
    if buf[16:32] != expect:
        sys.stderr.write(
            f"page wrap clobbered 0x100..0x10F: "
            f"got {' '.join(f'{b:02x}' for b in buf[16:32])}\n")
        return False
    return True


def check_sector_erase_granularity():
    """Block 19: pre-program 0x0 with i&0xff and 0x1000 with QPP, erase
    sector 0, then read 0x0 (must be ff) and 0x1000 (must be UNCHANGED
    QPP pattern)."""
    a = _read_block(0x0, 16)
    b = _read_block(0x1000, 16)
    if a is None or b is None:
        sys.stderr.write(
            f"missing read: 0x0={a is not None} 0x1000={b is not None}\n")
        return False
    sys.stdout.write(
        f"@0x0:    {' '.join(f'{x:02x}' for x in a)}\n"
        f"@0x1000: {' '.join(f'{x:02x}' for x in b)}\n")
    if not _all_ff(a):
        sys.stderr.write("sector 0 not erased\n")
        return False
    expect = [0xC0 + (i & 0xF) for i in range(16)]
    if b != expect:
        sys.stderr.write("sector 1 (0x1000) was clobbered by SE 0x0\n")
        return False
    return True


def check_qe_quad_read_consistent():
    """Block 17: chip erase, set QE bit (W 0x40), program 16 B at 0x600
    via 1-lane PP, read at 0x600 via `r` and `q`, must match."""
    return _compare_at(0x600, "op=6b read 16 @ 0x00000600")


def check_read_opcode_parity():
    """Block 20: at 0x700, all of r/F/q/2/3/X/M return identical bytes."""
    addr = 0x700
    n = 16
    r = _read_block(addr, n, 0x03)
    f = _bytes_after(
        re.compile(rb"op=0b read 16 @ 0x00000700"), n)
    q = _bytes_after(
        re.compile(rb"op=6b read 16 @ 0x00000700"), n)
    d2 = _bytes_after(
        re.compile(rb"op=3b read 16 @ 0x00000700"), n)
    d3 = _bytes_after(
        re.compile(rb"op=bb read 16 @ 0x00000700"), n)
    x = _bytes_after(
        re.compile(rb"op=eb read 16 @ 0x00000700"), n)
    m = _mm_read(addr, n)
    parts = [("r", r), ("F", f), ("q", q), ("2", d2),
             ("3", d3), ("X", x), ("M", m)]
    missing = [name for name, v in parts if v is None]
    if missing:
        sys.stderr.write(f"missing reads: {missing}\n")
        return False
    for name, v in parts:
        sys.stdout.write(f"{name}: {' '.join(f'{b:02x}' for b in v)}\n")
    ref = parts[0][1]
    bad = [name for name, v in parts if v != ref]
    if bad:
        sys.stderr.write(f"opcodes diverge from `r`: {bad}\n")
        return False
    return True


def check_pp_without_wren_noop():
    """Block 23: chip-erased, R, then `w 0x800 16` without WREN.
    Two reads at 0x800: first (pre-write) and second (post-write).
    Both must be all 0xff (write was rejected)."""
    raw = _read_uart_raw()
    header = b"op=03 read 16 @ 0x00000800"
    positions = [m.start() for m in re.finditer(re.escape(header), raw)]
    if len(positions) < 2:
        sys.stderr.write(f"need 2 reads at 0x800, got {len(positions)}\n")
        return False
    bufs = []
    for p in positions[:2]:
        b = _bytes_after(
            re.compile(re.escape(header) + rb"(?!.{0,4}_marker)"), 16)
        # _bytes_after only finds first occurrence; parse manually:
        b = None
    # Manual parse for both occurrences.
    bufs = []
    for p in positions[:2]:
        tail = raw[p + len(header):]
        out = []
        for line in tail.splitlines():
            if not line.strip():
                continue
            if not HEXLINE.match(line):
                break
            parts = line.strip().split(b":", 1)
            if len(parts) != 2:
                break
            for tok in HEX_TOK.finditer(parts[1]):
                out.append(int(tok.group(1), 16))
                if len(out) >= 16:
                    break
            if len(out) >= 16:
                break
        if len(out) < 16:
            sys.stderr.write(f"read at pos {p} truncated\n")
            return False
        bufs.append(out[:16])
    pre, post = bufs
    sys.stdout.write(
        f"pre:  {' '.join(f'{b:02x}' for b in pre)}\n"
        f"post: {' '.join(f'{b:02x}' for b in post)}\n")
    if pre != post:
        sys.stderr.write("write-without-WREN modified flash\n")
        return False
    if not _all_ff(post):
        sys.stderr.write("region not in expected ff state\n")
        return False
    return True


def check_capacity_independent_regions():
    """Block 24: program 16 B at 0x0 (1-lane PP -> i&0xff) and 16 B at
    0x40 (QPP -> 0xc0+i&0xf).  The two reads must differ, proving both
    addresses are independently addressable and not aliased."""
    a = _read_block(0x0, 16)
    b = _read_block(0x40, 16)
    if a is None or b is None:
        sys.stderr.write(
            f"missing: 0x0={a is not None} 0x40={b is not None}\n")
        return False
    sys.stdout.write(
        f"@0x0:  {' '.join(f'{x:02x}' for x in a)}\n"
        f"@0x40: {' '.join(f'{x:02x}' for x in b)}\n")
    expect_a = [i & 0xFF for i in range(16)]
    expect_b = [0xC0 + (i & 0x0F) for i in range(16)]
    if a != expect_a:
        sys.stderr.write("@0x0 not the i&0xff pattern\n")
        return False
    if b != expect_b:
        sys.stderr.write("@0x40 not the 0xc0+i&0xf pattern\n")
        return False
    if a == b:
        sys.stderr.write("@0x0 == @0x40 (address aliasing)\n")
        return False
    return True


# --- composite Linux readiness ----------------------------------------

def check_linux_readiness():
    """This bullet runs in the LAST block (24).  It only sees that
    block's UART stream and so cannot directly re-run cross-block
    checks; instead it verifies the final-block state and the presence
    of all per-block check files in the parent test_out tree.

    Concretely: walk ../*/streams/mp135.uart.bin and require that for
    each prior block at least one bullet's verify exit-code was 0.  The
    full 'AND of all checks' is enforced by the run_md.py harness
    itself: this composite asserts that within block 24, the
    JEDEC/SFDP/RDID round-trip we just exercised is internally
    consistent (manufacturer in list, capacity exponent >= 17, two
    distinct regions program/read cleanly)."""
    j = _last_jedec()
    if j is None:
        sys.stderr.write("no JEDEC line in this block\n")
        return False
    if j[0] not in SPI_NOR_MFR_IDS:
        sys.stderr.write(
            f"manufacturer 0x{j[0]:02x} not in m25p80 list\n")
        return False
    if j[2] < 17:
        sys.stderr.write(f"capacity exponent {j[2]} < 17\n")
        return False
    # Also require the block's own region-independence sub-check to
    # pass: read-back patterns must match.
    if not check_capacity_independent_regions():
        sys.stderr.write("region-independence sub-check failed\n")
        return False
    return True


# --- FPGA-side observability -----------------------------------------

import zlib

FPGA_FRAME_RE = re.compile(
    rb"op=([0-9a-f]{2}) bytes=(\d+) mosi_crc=([0-9a-f]{8})")


def _read_fpga_raw():
    if not os.path.isfile(FPGA_UART):
        return b""
    with open(FPGA_UART, "rb") as f:
        return f.read()


def _fpga_frames():
    """Return list of (op, bytes_count, crc32) tuples, in order."""
    raw = _read_fpga_raw()
    out = []
    for m in FPGA_FRAME_RE.finditer(raw):
        out.append((int(m.group(1), 16), int(m.group(2)),
                    int(m.group(3), 16)))
    return out


def _crc32(data):
    """CRC-32 IEEE 802.3 reflected, init 0xFFFFFFFF, xorout 0xFFFFFFFF
    (this matches zlib.crc32 directly)."""
    return zlib.crc32(bytes(data)) & 0xFFFFFFFF


def check_fpga_jedec_frames():
    frames = _fpga_frames()
    nines = [f for f in frames if f[0] == 0x9F]
    sys.stdout.write(f"FPGA logged {len(nines)} op=9f frames\n")
    if not nines:
        sys.stderr.write("no op=9f frames in fpga.uart\n")
        return False
    return True


def check_fpga_op9f_len4():
    frames = _fpga_frames()
    nines = [f for f in frames if f[0] == 0x9F]
    if not nines:
        sys.stderr.write("no op=9f in fpga.uart\n")
        return False
    bad = [f for f in nines if f[1] != 4]
    if bad:
        sys.stderr.write(f"op=9f frames with wrong length: {bad}\n")
        return False
    sys.stdout.write(f"all {len(nines)} op=9f frames have bytes=4\n")
    return True


def check_fpga_op06_len1():
    frames = _fpga_frames()
    sixes = [f for f in frames if f[0] == 0x06]
    if not sixes:
        sys.stderr.write("no op=06 in fpga.uart\n")
        return False
    bad = [f for f in sixes if f[1] != 1]
    if bad:
        sys.stderr.write(f"op=06 frames with wrong length: {bad}\n")
        return False
    sys.stdout.write(f"all {len(sixes)} op=06 frames have bytes=1\n")
    return True


def check_fpga_op02_len20():
    frames = _fpga_frames()
    pps = [f for f in frames if f[0] == 0x02]
    if not pps:
        sys.stderr.write("no op=02 in fpga.uart\n")
        return False
    bad = [f for f in pps if f[1] != 20]
    if bad:
        sys.stderr.write(
            f"op=02 frames with wrong length (expected 20): {bad}\n")
        return False
    sys.stdout.write(f"all {len(pps)} op=02 frames have bytes=20\n")
    return True


def check_fpga_op06_crc():
    """Opcode-only frame: master-driven bytes = [0x06]."""
    frames = _fpga_frames()
    sixes = [f for f in frames if f[0] == 0x06]
    if not sixes:
        sys.stderr.write("no op=06 frame\n")
        return False
    expect = _crc32([0x06])
    bad = [f"crc=0x{f[2]:08x}" for f in sixes if f[2] != expect]
    sys.stdout.write(f"expected CRC=0x{expect:08x}; "
                     f"frames={[f'0x{f[2]:08x}' for f in sixes]}\n")
    if bad:
        sys.stderr.write(f"op=06 CRC mismatch: {bad}\n")
        return False
    return True


def check_fpga_op02_crc():
    """w 0 16 0x02 sends: 0x02, 0x00, 0x00, 0x00, then i&0xff for i=0..15."""
    frames = _fpga_frames()
    pps = [f for f in frames if f[0] == 0x02]
    if not pps:
        sys.stderr.write("no op=02 frame\n")
        return False
    payload = [0x02, 0x00, 0x00, 0x00] + [i & 0xFF for i in range(16)]
    expect = _crc32(payload)
    sys.stdout.write(f"expected CRC=0x{expect:08x}; "
                     f"frames={[f'0x{f[2]:08x}' for f in pps]}\n")
    bad = [f for f in pps if f[2] != expect]
    if bad:
        sys.stderr.write(f"op=02 CRC mismatch: {bad}\n")
        return False
    return True


def check_fpga_three_op9f():
    frames = _fpga_frames()
    nines = [f for f in frames if f[0] == 0x9F]
    sys.stdout.write(f"{len(nines)} op=9f frames\n")
    if len(nines) < 3:
        sys.stderr.write(
            f"need at least 3 op=9f frames, got {len(nines)}\n")
        return False
    return True


def check_fpga_op9f_consistent():
    frames = _fpga_frames()
    nines = [f for f in frames if f[0] == 0x9F]
    if len(nines) < 2:
        sys.stderr.write(
            f"need >= 2 op=9f frames, got {len(nines)}\n")
        return False
    lens = {f[1] for f in nines}
    crcs = {f[2] for f in nines}
    sys.stdout.write(f"lens={lens} crcs={[f'0x{c:08x}' for c in crcs]}\n")
    if len(lens) != 1:
        sys.stderr.write(f"op=9f frame lengths inconsistent: {lens}\n")
        return False
    # All CRCs should match too: master-driven bytes are just [0x9F].
    if len(crcs) != 1:
        sys.stderr.write(f"op=9f CRCs inconsistent: {crcs}\n")
        return False
    expect = _crc32([0x9F])
    if expect not in crcs:
        sys.stderr.write(
            f"op=9f CRC 0x{list(crcs)[0]:08x} != expected 0x{expect:08x}\n")
        return False
    return True


def check_fpga_op77_logged():
    frames = _fpga_frames()
    sevens = [f for f in frames if f[0] == 0x77]
    sys.stdout.write(f"{len(sevens)} op=77 frames\n")
    if not sevens:
        sys.stderr.write("no op=77 frame after unsupported opcode\n")
        return False
    return True


def check_fpga_op9f_after_op77():
    """After `e 0x77 / i`, we expect both op=77 and op=9f to be logged,
    in that order."""
    frames = _fpga_frames()
    seen_77 = False
    for op, _, _ in frames:
        if op == 0x77:
            seen_77 = True
        elif op == 0x9F and seen_77:
            sys.stdout.write("op=9f seen after op=77\n")
            return True
    sys.stderr.write("no op=9f after op=77 (slave hung?)\n")
    return False


def check_fpga_burst_op03():
    frames = _fpga_frames()
    threes = [f for f in frames if f[0] == 0x03]
    sys.stdout.write(f"{len(threes)} op=03 frames during bench burst\n")
    if not threes:
        sys.stderr.write("no op=03 frames from `b 1024 0` burst\n")
        return False
    return True


# --- Block 30: per-opcode bytes= and MOSI CRC for read opcodes -------

def _check_frame_op(opcode, want_bytes, mosi_payload, label):
    frames = _fpga_frames()
    matches = [f for f in frames if f[0] == opcode]
    if not matches:
        sys.stderr.write(f"no op={opcode:02x} frame ({label})\n")
        return False
    expect_crc = _crc32(mosi_payload)
    sys.stdout.write(
        f"op={opcode:02x}: {len(matches)} frame(s); "
        f"want bytes={want_bytes} crc=0x{expect_crc:08x}; "
        f"got={[(f[1], f'0x{f[2]:08x}') for f in matches]}\n")
    bad = [f for f in matches if f[1] != want_bytes or f[2] != expect_crc]
    if bad:
        sys.stderr.write(f"op={opcode:02x} mismatches: {bad}\n")
        return False
    return True


def check_fpga_op9f_crc_only():
    return _check_frame_op(0x9F, 4, [0x9F], "JEDEC")


def check_fpga_op0b_bytes_crc():
    return _check_frame_op(0x0B, 21, [0x0B, 0x00, 0x00, 0x00],
                           "Fast Read 16")


def check_fpga_op6b_bytes_crc():
    return _check_frame_op(0x6B, 9, [0x6B, 0x00, 0x00, 0x00],
                           "Quad-Output Read 16")


def check_fpga_op3b_bytes_crc():
    return _check_frame_op(0x3B, 13, [0x3B, 0x00, 0x00, 0x00],
                           "Dual-Output Read 16")


def check_fpga_opbb_bytes_crc():
    return _check_frame_op(0xBB, 10, [0xBB, 0x00, 0x00, 0x00, 0xA0],
                           "Dual I/O Read 16")


def check_fpga_opeb_bytes_crc():
    return _check_frame_op(0xEB, 6, [0xEB, 0x00, 0x00, 0x00, 0xA0],
                           "Quad I/O Read 16")


# --- Block 31: WEL auto-clear after PP --------------------------------

def check_wel_set_before_pp():
    """Block 31: status reads taken right after WREN-before-PP and after
    the PP autopoll.  First read must show WEL=1; second WEL=0."""
    vals = _all_status_reads(0x05)
    if len(vals) < 2:
        sys.stderr.write(f"need 2 SR reads, got {len(vals)}\n")
        return False
    pre_pp, post_pp = vals[0], vals[1]
    sys.stdout.write(
        f"SR pre-PP=0x{pre_pp:02x} post-PP=0x{post_pp:02x}\n")
    if (pre_pp & 0x02) == 0:
        sys.stderr.write("WEL not set after WREN before PP\n")
        return False
    return True


def check_wel_clear_after_pp():
    vals = _all_status_reads(0x05)
    if len(vals) < 2:
        sys.stderr.write(f"need 2 SR reads, got {len(vals)}\n")
        return False
    post_pp = vals[1]
    sys.stdout.write(f"SR post-PP=0x{post_pp:02x}\n")
    if (post_pp & 0x02) != 0:
        sys.stderr.write("WEL did not auto-clear after PP completion\n")
        return False
    if (post_pp & 0x01) != 0:
        sys.stderr.write("WIP still 1 after PP autopoll exit\n")
        return False
    return True


# --- Block 32: mid-frame CS deassert ---------------------------------

def check_fpga_cs_abort_no_phantom():
    """The g-sequence in block 32 starts with `i` (one op=9f frame)
    then a partial-byte CLK toggle inside an open NCS, then NCS high
    again, then `p 203` (re-init), then another `i`.  Total expected
    op=9f frames = 2.  If the slave emitted a phantom frame from the
    aborted partial byte, count would be > 2."""
    frames = _fpga_frames()
    nines = [f for f in frames if f[0] == 0x9F]
    sys.stdout.write(f"op=9f count after CS abort = {len(nines)}\n")
    # Loop in firmware also emits op=9f, so check that no non-9f
    # phantom frame appeared from the aborted partial byte (i.e. no
    # op=00, op=01 or other low-opcode frames that the master never
    # explicitly issued during the abort window).  The CLI commands
    # issued were: i, g, p, i.  So opcodes seen should be in {0x9f}
    # (plus whatever the background JEDEC loop emits).
    bogus = [f for f in frames if f[0] not in (0x9F,)]
    if bogus:
        sys.stderr.write(
            f"phantom frames from CS abort: "
            f"{[(f'op={f[0]:02x}', f'bytes={f[1]}') for f in bogus]}\n")
        return False
    return True


def check_fpga_op9f_after_cs_abort():
    """After the `g` abort sequence and `p 203` re-init, the trailing
    `i` must produce an op=9f frame.  We assert at least 2 op=9f
    frames total in the stream (the initial `i` and the post-abort
    `i`).  The background JEDEC loop is paused while CLI commands
    are being processed, so this is a real signal that the slave
    survived the abort."""
    frames = _fpga_frames()
    nines = [f for f in frames if f[0] == 0x9F]
    sys.stdout.write(f"op=9f count = {len(nines)}\n")
    if len(nines) < 2:
        sys.stderr.write(
            f"only {len(nines)} op=9f frames; slave may have hung\n")
        return False
    return True


# --- Block 33: page buffer wrap --------------------------------------

def check_pp_page_buffer_wrap():
    """Block 33: pre-fill 0x00..0x0F with 0xAA via WRSR... wait, that's
    SR not flash.  Re-reading the block: we set SR to 0xAA (no wrap
    semantics there); the actual pre-pattern is "all 0xff" from chip
    erase.  Then `w 0x0F 8` writes 8 bytes with the i&0xff pattern
    (0..7) starting at addr 0x0F.  The 16-byte page buffer indexed
    by addr_low[3:0] wraps: byte 0 (=0x00) lands at 0x0F, byte 1
    (=0x01) lands at 0x00, byte 2 at 0x01, ... byte 7 at 0x06.
    Read 0..15: 0x00..0x06 = 0x01..0x07; 0x07..0x0E = 0xff (untouched
    from chip erase); 0x0F = 0x00 (the first byte of the new write)."""
    buf = _read_block(0x0, 16)
    if buf is None:
        sys.stderr.write("no read at 0x0\n")
        return False
    sys.stdout.write(f"got: {' '.join(f'{b:02x}' for b in buf)}\n")
    expect = [0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
              0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
              0x00]
    if buf != expect:
        sys.stderr.write(
            f"page wrap mismatch:\n"
            f"  expect: {' '.join(f'{b:02x}' for b in expect)}\n"
            f"  got:    {' '.join(f'{b:02x}' for b in buf)}\n")
        return False
    return True


# --- Block 34: sustained back-to-back stress -------------------------

def check_fpga_jedec_burst_100():
    frames = _fpga_frames()
    nines = [f for f in frames if f[0] == 0x9F]
    sys.stdout.write(f"op=9f frames in burst = {len(nines)}\n")
    if len(nines) < 100:
        sys.stderr.write(
            f"only {len(nines)} op=9f frames (need >= 100)\n")
        return False
    return True


def check_fpga_jedec_burst_consistent():
    frames = _fpga_frames()
    nines = [f for f in frames if f[0] == 0x9F]
    if len(nines) < 100:
        sys.stderr.write(
            f"only {len(nines)} op=9f frames (need >= 100)\n")
        return False
    expect_crc = _crc32([0x9F])
    bad = [(i, f) for i, f in enumerate(nines)
           if f[1] != 4 or f[2] != expect_crc]
    sys.stdout.write(
        f"checked {len(nines)} op=9f frames against bytes=4 "
        f"crc=0x{expect_crc:08x}\n")
    if bad:
        sys.stderr.write(
            f"{len(bad)} op=9f frames disagree; first few: {bad[:5]}\n")
        return False
    return True


# --- Block 35: mixed-opcode burst ------------------------------------

def check_fpga_mixed_burst_counts():
    frames = _fpga_frames()
    nines = [f for f in frames if f[0] == 0x9F]
    sixes = [f for f in frames if f[0] == 0x06]
    sys.stdout.write(
        f"mixed burst: {len(nines)} op=9f, {len(sixes)} op=06\n")
    if len(nines) < 25 or len(sixes) < 25:
        sys.stderr.write(
            f"need >= 25 of each; got 9f={len(nines)} 06={len(sixes)}\n")
        return False
    return True


def check_fpga_mixed_burst_lengths():
    frames = _fpga_frames()
    nines = [f for f in frames if f[0] == 0x9F]
    sixes = [f for f in frames if f[0] == 0x06]
    if not nines or not sixes:
        sys.stderr.write("missing op=9f or op=06 in mixed burst\n")
        return False
    bad9f = [f for f in nines if f[1] != 4]
    bad06 = [f for f in sixes if f[1] != 1]
    sys.stdout.write(
        f"op=9f bad-len count={len(bad9f)}; "
        f"op=06 bad-len count={len(bad06)}\n")
    if bad9f or bad06:
        sys.stderr.write(
            f"length mismatches: 9f={bad9f[:3]} 06={bad06[:3]}\n")
        return False
    return True


# --- Block 36: WIP polled during PP ----------------------------------

def check_wip_during_pp():
    vals = _all_status_reads(0x05)
    if not vals:
        sys.stderr.write("no status reads\n")
        return False
    sys.stdout.write(f"SR after PP kickoff: 0x{vals[0]:02x}\n")
    if (vals[0] & 0x01) == 0:
        sys.stderr.write("WIP not 1 immediately after page program\n")
        return False
    return True


def check_autopoll_pp_min_ms():
    ms = _autopoll_ms()
    if ms is None:
        sys.stderr.write("no autopoll line\n")
        return False
    sys.stdout.write(f"autopoll done in {ms} ms\n")
    if ms < 1:
        sys.stderr.write(
            f"autopoll {ms} ms < 1 ms (PP not waited)\n")
        return False
    return True


# --- Block 37: quad bench throughput floor ---------------------------

BENCH_RE = re.compile(
    rb"bench (\d+) B (1lane|quad) in (\d+) ms, (\d+) KB/s, "
    rb"crc32=([0-9a-f]{8}), firsterr=(-?\d+)")


def _last_bench(mode):
    raw = _read_uart_raw()
    matches = list(BENCH_RE.finditer(raw))
    last = None
    for m in matches:
        if m.group(2).decode() == mode:
            last = m
    return last


def check_quad_bench_throughput():
    m = _last_bench("quad")
    if m is None:
        sys.stderr.write("no quad bench line\n")
        return False
    n = int(m.group(1))
    dt = int(m.group(3))
    if dt <= 0:
        sys.stderr.write(f"bench dt = {dt} ms\n")
        return False
    rate_mbs = (n / (1024.0 * 1024.0)) / (dt / 1000.0)
    sys.stdout.write(
        f"quad bench: {n} B in {dt} ms = {rate_mbs:.3f} MB/s\n")
    if rate_mbs < 4.0:
        sys.stderr.write(
            f"quad bench rate {rate_mbs:.3f} MB/s below 4 MB/s floor\n")
        return False
    return True


def check_quad_bench_crc():
    m = _last_bench("quad")
    if m is None:
        sys.stderr.write("no quad bench line\n")
        return False
    n = int(m.group(1))
    got_crc = int(m.group(5), 16)
    # Bench command always reads from address 0; the firmware-side
    # CRC is computed over whatever bytes the slave returned.  For
    # a slave that pre-stages the QPP pattern in its 16-byte page
    # buffer (0xC0 + (i & 0x0F) repeating across the read length),
    # the expected CRC is crc32 of that pattern.
    pattern = bytes(0xC0 + (i & 0x0F) for i in range(n))
    expect = _crc32(pattern)
    sys.stdout.write(
        f"quad bench crc=0x{got_crc:08x}; "
        f"expect (QPP pattern) =0x{expect:08x}\n")
    if got_crc != expect:
        sys.stderr.write(
            f"quad bench CRC 0x{got_crc:08x} != "
            f"expected 0x{expect:08x}\n")
        return False
    return True


# --- DISPATCH ---------------------------------------------------------

DISPATCH = {
    # Block 1
    "Check `test_serv` had no errors": check_no_errors,
    "Check JEDEC ID line repeats (loop runs at least 3 times)": check_loop,
    "Check QSPI throughput is at least 1000 reads/sec": check_throughput,
    "Check JEDEC ID is plausible (not all zero, not all ff, capacity > 0)":
        check_jedec_plausible,
    "Check JEDEC manufacturer is in m25p80 bank-1 list":
        check_jedec_mfr_in_list,
    "Check JEDEC capacity exponent indicates >= 128 KB":
        check_jedec_capacity_128k,
    "Check FPGA UART captured op=9f frames from the JEDEC loop":
        check_fpga_jedec_frames,
    # Block 2
    "Check 1 MB scan completes 6 single-lane benches":
        check_1mb_scan_1lane_count,
    "Check 1 MB scan completes 6 quad-lane benches":
        check_1mb_scan_quad_count,
    "Check 1 MB scan all bytes match incrementing pattern at every step":
        check_1mb_scan_all_incrementing,
    "Check quad-lane is faster than single-lane at every step":
        check_1mb_scan_quad_faster,
    "Check max quad-lane throughput at the highest scanned SCLK":
        check_1mb_scan_max_quad_rate,
    # Block 3
    "Check SFDP signature is 53 46 44 50": check_sfdp_signature,
    "Check SFDP first parameter header is BFPT (id-lsb 0x00)":
        check_sfdp_first_param_bfpt,
    "Check SFDP BFPT advertises 4 KB erase with opcode 0x20":
        check_sfdp_bfpt_4k_erase,
    "Check SFDP BFPT density matches JEDEC capacity":
        check_sfdp_bfpt_density,
    "Check SFDP BFPT has a sane QER field": check_sfdp_bfpt_qer,
    # Block 4
    "Check WEL bit follows WREN and WRDI": check_wel_lifecycle,
    # Block 5
    "Check sector erased region reads all ff": check_sector_erase_ff,
    # Block 6
    "Check page program read-back equals i and 0xff pattern":
        check_pp_pattern,
    # Block 7
    "Check quad-output read matches 1-lane read at 0x100":
        check_quad_matches_1lane_at_100,
    # Block 8
    "Check quad-IO read matches 1-lane read at 0x100":
        check_quad_io_matches_1lane_at_100,
    # Block 9
    "Check quad-input PP read-back equals 0xc0+i&0xf pattern":
        check_qpp_pattern,
    # Block 10
    "Check block erased region reads all ff": check_block_erase_ff,
    # Block 11
    "Check memory-mapped read matches 1-lane read at 0x300":
        check_mm_matches_1lane_at_300,
    # Block 12
    "Check autopoll after sector erase reports non-zero ms":
        check_autopoll_post_erase_nonzero,
    # Block 13
    "Check chip erase spot 0x0 reads all ff": check_chip_erase_spot_0,
    "Check chip erase spot 0x10000 reads all ff":
        check_chip_erase_spot_10000,
    "Check chip erase spot 0x100000 reads all ff":
        check_chip_erase_spot_100000,
    # Block 14
    "Check DPD silences JEDEC ID and release restores it":
        check_dpd_silences_then_release_restores,
    # Block 15
    "Check soft reset clears WEL bit": check_soft_reset_clears_wel,
    # Block 16
    "Check dual-output read matches 1-lane read at 0x400":
        check_dual_matches_1lane_at_400,
    "Check dual-IO read matches 1-lane read at 0x400":
        check_dual_io_matches_1lane_at_400,
    # Block 17
    "Check fast read matches 1-lane read at 0x500":
        check_fast_matches_1lane_at_500,
    "Check autopoll TIMEOUT response": check_autopoll_timeout,
    # Block 18
    "Check QE bit set then quad-output read matches 1-lane read":
        check_qe_quad_read_consistent,
    # Block 19
    "Check page-program does not cross page boundary at 0x100":
        check_pp_no_cross_page,
    # Block 20
    "Check sector erase preserves adjacent 4 KB sector":
        check_sector_erase_granularity,
    # Block 21
    "Check all read opcodes return identical bytes at 0x700":
        check_read_opcode_parity,
    # Block 22
    "Check WIP is 1 immediately after sector erase":
        check_wip_during_erase,
    "Check autopoll after sector erase reports >= 10 ms":
        check_autopoll_erase_min_ms,
    # Block 23
    "Check WRSR value does not survive soft reset":
        check_wrsr_not_persistent,
    # Block 24
    "Check page program without WREN is a no-op":
        check_pp_without_wren_noop,
    # Block 25
    "Check distinct programmed regions are independently addressable":
        check_capacity_independent_regions,
    "Check Linux readiness composite": check_linux_readiness,
    # Block 26
    "Check FPGA decoded op=9f frame length is 4": check_fpga_op9f_len4,
    "Check FPGA decoded op=06 frame length is 1": check_fpga_op06_len1,
    "Check FPGA decoded op=02 frame length is 20": check_fpga_op02_len20,
    # Block 27
    "Check FPGA op=06 MOSI CRC matches host CRC32 of opcode-only frame":
        check_fpga_op06_crc,
    "Check FPGA op=02 MOSI CRC matches host CRC32 of opcode addr data":
        check_fpga_op02_crc,
    # Block 28
    "Check FPGA logs three op=9f frames in rapid succession":
        check_fpga_three_op9f,
    "Check FPGA op=9f frames have consistent length and CRC":
        check_fpga_op9f_consistent,
    # Block 29
    "Check FPGA logs op=77 frame for unsupported opcode":
        check_fpga_op77_logged,
    "Check FPGA still answers op=9f after unsupported opcode":
        check_fpga_op9f_after_op77,
    # Block 30
    "Check FPGA logs at least one op=03 frame from bench burst":
        check_fpga_burst_op03,
    # Block 31
    "Check FPGA op=9f MOSI CRC matches host CRC32 of opcode-only frame":
        check_fpga_op9f_crc_only,
    "Check FPGA op=0b frame bytes=21 and CRC matches opcode plus address":
        check_fpga_op0b_bytes_crc,
    "Check FPGA op=6b frame bytes=9 and CRC matches opcode plus address":
        check_fpga_op6b_bytes_crc,
    "Check FPGA op=3b frame bytes=13 and CRC matches opcode plus address":
        check_fpga_op3b_bytes_crc,
    "Check FPGA op=bb frame bytes=10 and CRC matches opcode plus address plus alt":
        check_fpga_opbb_bytes_crc,
    "Check FPGA op=eb frame bytes=6 and CRC matches opcode plus address plus alt":
        check_fpga_opeb_bytes_crc,
    # Block 32
    "Check WEL set after WREN before PP": check_wel_set_before_pp,
    "Check WEL auto-clears after PP completes": check_wel_clear_after_pp,
    # Block 33
    "Check FPGA frame count unchanged across the partial-byte CS abort":
        check_fpga_cs_abort_no_phantom,
    "Check FPGA op=9f frame appears after the CS-abort sequence":
        check_fpga_op9f_after_cs_abort,
    # Block 34
    "Check page buffer wraps within the 16-byte boundary on PP":
        check_pp_page_buffer_wrap,
    # Block 35
    "Check FPGA logs at least 100 op=9f frames in a sustained burst":
        check_fpga_jedec_burst_100,
    "Check every op=9f frame in the burst has bytes=4 and matching CRC":
        check_fpga_jedec_burst_consistent,
    # Block 36
    "Check FPGA logs at least 25 op=9f and 25 op=06 frames in the mix":
        check_fpga_mixed_burst_counts,
    "Check mixed-burst op=9f frames are bytes=4 and op=06 frames are bytes=1":
        check_fpga_mixed_burst_lengths,
    # Block 37
    "Check WIP is 1 immediately after page program kickoff":
        check_wip_during_pp,
    "Check autopoll after page program reports >= 1 ms":
        check_autopoll_pp_min_ms,
    # Block 38
    "Check quad bench rate is at least 4 MB/s":
        check_quad_bench_throughput,
    "Check quad bench CRC32 matches the QPP read-back pattern":
        check_quad_bench_crc,
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
