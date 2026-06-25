#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Jakob Kastelic
"""Package-free WebSocket receiver for the stream_ws_prbs_stream server.

Connects to a server that, after the WebSocket opening handshake, pushes a
fixed number of deterministic PRBS bytes as unmasked binary frames (see
stm32mp135_test_board/tools/stream_ws_prbs_stream.c). Receives exactly
--bytes of payload, computes SHA-256 and CRC32 while reading, and exits
non-zero unless the digests match the expected values and the wall-time
payload rate meets --min-rate-Bps. No third-party packages (raw socket +
hashlib + zlib + base64).
"""
import argparse
import base64
import hashlib
import os
import socket
import sys
import time
import zlib


def http_handshake(sock, host, port):
    key = base64.b64encode(os.urandom(16)).decode()
    req = (
        "GET / HTTP/1.1\r\n"
        f"Host: {host}:{port}\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n"
    )
    sock.sendall(req.encode())

    # Read response headers; keep any frame bytes that arrive in the same recv.
    buf = b""
    while b"\r\n\r\n" not in buf:
        chunk = sock.recv(4096)
        if not chunk:
            raise RuntimeError("server closed during handshake")
        buf += chunk
    head, _, leftover = buf.partition(b"\r\n\r\n")
    status = head.split(b"\r\n", 1)[0]
    if b"101" not in status:
        raise RuntimeError("bad handshake status: %r" % status)
    # Verify Sec-WebSocket-Accept (defensive; RFC 6455 GUID).
    accept = base64.b64encode(
        hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode())
        .digest()
    ).decode()
    if accept.encode() not in head:
        raise RuntimeError("Sec-WebSocket-Accept mismatch")
    return leftover


class Reader:
    """Buffered reader over a socket, seeded with handshake leftover."""

    def __init__(self, sock, leftover=b""):
        self.sock = sock
        self.buf = bytearray(leftover)

    def read_exact(self, n):
        while len(self.buf) < n:
            chunk = self.sock.recv(max(65536, n - len(self.buf)))
            if not chunk:
                raise RuntimeError("server closed mid-frame")
            self.buf += chunk
        out = bytes(self.buf[:n])
        del self.buf[:n]
        return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, required=True)
    ap.add_argument("--bytes", type=int, required=True)
    ap.add_argument("--min-rate-Bps", type=float, default=1.0)
    ap.add_argument("--expect-sha256", default=None)
    ap.add_argument("--expect-crc32", default=None)
    args = ap.parse_args()

    sock = socket.create_connection((args.host, args.port), timeout=30)
    sock.settimeout(30)
    leftover = http_handshake(sock, args.host, args.port)
    rd = Reader(sock, leftover)

    sha = hashlib.sha256()
    crc = 0
    got = 0
    t0 = None
    while got < args.bytes:
        h = rd.read_exact(2)
        opcode = h[0] & 0x0F
        masked = (h[1] & 0x80) != 0
        ln = h[1] & 0x7F
        if ln == 126:
            ln = int.from_bytes(rd.read_exact(2), "big")
        elif ln == 127:
            ln = int.from_bytes(rd.read_exact(8), "big")
        mask = rd.read_exact(4) if masked else None
        payload = rd.read_exact(ln) if ln else b""
        if mask:
            payload = bytes(b ^ mask[i & 3] for i, b in enumerate(payload))

        if opcode == 0x8:  # close
            break
        if opcode not in (0x1, 0x2, 0x0):  # ping/pong/other: ignore
            continue
        if not payload:
            continue
        if t0 is None:
            t0 = time.monotonic()
        take = min(len(payload), args.bytes - got)
        chunk = payload[:take]
        sha.update(chunk)
        crc = zlib.crc32(chunk, crc)
        got += take

    elapsed = (time.monotonic() - t0) if t0 else 0.0
    crc &= 0xFFFFFFFF
    digest = sha.hexdigest()
    rate = (got / elapsed) if elapsed > 0 else float("inf")

    print("ws_recv bytes=%d sha256=%s crc32=0x%08x elapsed=%.3fs rate=%.0f B/s"
          % (got, digest, crc, elapsed, rate))

    ok = got == args.bytes
    if args.expect_sha256 and digest != args.expect_sha256.lower():
        print("FAIL sha256 mismatch (got %s want %s)" % (digest, args.expect_sha256))
        ok = False
    if args.expect_crc32 is not None:
        want = int(args.expect_crc32, 0) & 0xFFFFFFFF
        if crc != want:
            print("FAIL crc32 mismatch (got 0x%08x want 0x%08x)" % (crc, want))
            ok = False
    if rate < args.min_rate_Bps:
        print("FAIL rate %.0f < min %.0f B/s" % (rate, args.min_rate_Bps))
        ok = False

    try:
        sock.close()
    except OSError:
        pass
    print("PASS" if ok else "FAIL")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
