#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Jakob Kastelic
"""Minimal WebSocket receiver for stream_ws_prbs_stream tests."""

import argparse
import base64
import binascii
import hashlib
import os
import socket
import struct
import time

GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument("--host", required=True)
    p.add_argument("--port", type=int, required=True)
    p.add_argument("--bytes", type=int, required=True)
    p.add_argument("--min-rate-Bps", type=float, default=0)
    p.add_argument("--expect-sha256")
    p.add_argument("--expect-crc32")
    p.add_argument("--timeout", type=float, default=30.0)
    return p.parse_args()


def recv_exact(sock, n):
    chunks = []
    got = 0
    while got < n:
        b = sock.recv(n - got)
        if not b:
            raise EOFError("unexpected EOF")
        chunks.append(b)
        got += len(b)
    return b"".join(chunks)


def handshake(sock, host, port):
    raw_key = os.urandom(16)
    key = base64.b64encode(raw_key).decode("ascii")
    req = (
        f"GET / HTTP/1.1\r\n"
        f"Host: {host}:{port}\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n"
    ).encode("ascii")
    sock.sendall(req)

    data = b""
    while b"\r\n\r\n" not in data:
        data += sock.recv(4096)
        if not data:
            raise RuntimeError("no handshake response")
        if len(data) > 16384:
            raise RuntimeError("oversized handshake response")

    header = data.decode("iso-8859-1")
    lines = header.split("\r\n")
    if not lines[0].startswith("HTTP/1.1 101"):
        raise RuntimeError(f"bad handshake status: {lines[0]}")
    headers = {}
    for line in lines[1:]:
        if ":" in line:
            k, v = line.split(":", 1)
            headers[k.strip().lower()] = v.strip()
    expect = base64.b64encode(hashlib.sha1((key + GUID).encode("ascii")).digest()).decode("ascii")
    if headers.get("sec-websocket-accept") != expect:
        raise RuntimeError("bad Sec-WebSocket-Accept")


def recv_frame(sock):
    h = recv_exact(sock, 2)
    b0, b1 = h[0], h[1]
    opcode = b0 & 0x0F
    masked = bool(b1 & 0x80)
    n = b1 & 0x7F
    if n == 126:
        n = struct.unpack("!H", recv_exact(sock, 2))[0]
    elif n == 127:
        n = struct.unpack("!Q", recv_exact(sock, 8))[0]
    mask = recv_exact(sock, 4) if masked else None
    payload = recv_exact(sock, n)
    if mask:
        payload = bytes(c ^ mask[i & 3] for i, c in enumerate(payload))
    return opcode, payload


def parse_crc32(s):
    if s is None:
        return None
    return int(s, 0) & 0xFFFFFFFF


def main():
    args = parse_args()
    want_crc = parse_crc32(args.expect_crc32)
    sha = hashlib.sha256()
    crc = 0
    total = 0

    start = time.monotonic()
    with socket.create_connection((args.host, args.port), timeout=args.timeout) as sock:
        sock.settimeout(args.timeout)
        handshake(sock, args.host, args.port)
        while total < args.bytes:
            opcode, payload = recv_frame(sock)
            if opcode == 0x8:
                raise RuntimeError("websocket closed before expected byte count")
            if opcode not in (0x0, 0x2):
                raise RuntimeError(f"unexpected opcode {opcode}")
            need = args.bytes - total
            if len(payload) > need:
                raise RuntimeError("received more payload than expected")
            sha.update(payload)
            crc = binascii.crc32(payload, crc)
            total += len(payload)

    elapsed = time.monotonic() - start
    rate = total / elapsed if elapsed > 0 else float("inf")
    got_sha = sha.hexdigest()
    got_crc = crc & 0xFFFFFFFF

    print(f"received bytes={total} seconds={elapsed:.6f} rate_Bps={rate:.2f}")
    print(f"sha256={got_sha}")
    print(f"crc32=0x{got_crc:08x}")

    if total != args.bytes:
        raise SystemExit(f"byte count mismatch: got {total}, expected {args.bytes}")
    if args.expect_sha256 and got_sha.lower() != args.expect_sha256.lower():
        raise SystemExit("sha256 mismatch")
    if want_crc is not None and got_crc != want_crc:
        raise SystemExit("crc32 mismatch")
    if args.min_rate_Bps and rate < args.min_rate_Bps:
        raise SystemExit(f"rate too low: {rate:.2f} < {args.min_rate_Bps}")


if __name__ == "__main__":
    main()
