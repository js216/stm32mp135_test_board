// SPDX-License-Identifier: MIT
// Minimal WebSocket PRBS streamer for STM32MP135 Ethernet throughput tests.
// Copyright (c) 2026 Jakob Kastelic

#include <arpa/inet.h>
#include <errno.h>
#include <getopt.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define DEFAULT_PORT 8765
#define DEFAULT_BYTES (128ULL * 1024ULL * 1024ULL)
#define DEFAULT_SEED 0x12345678u
#define FRAME_PAYLOAD (32U * 1024U)
#define MAX_FRAME_PAYLOAD (1024U * 1024U)
#define READ_BUF 4096

struct sha1_ctx {
	uint32_t h[5];
	uint64_t len;
	uint8_t block[64];
	size_t used;
};

static uint32_t rol32(uint32_t v, unsigned int n)
{
	return (v << n) | (v >> (32 - n));
}

static void sha1_init(struct sha1_ctx *ctx)
{
	ctx->h[0] = 0x67452301u;
	ctx->h[1] = 0xefcdab89u;
	ctx->h[2] = 0x98badcfeu;
	ctx->h[3] = 0x10325476u;
	ctx->h[4] = 0xc3d2e1f0u;
	ctx->len = 0;
	ctx->used = 0;
}

static void sha1_block(struct sha1_ctx *ctx, const uint8_t block[64])
{
	uint32_t w[80];
	uint32_t a, b, c, d, e;

	for (int i = 0; i < 16; i++) {
		w[i] = ((uint32_t)block[i * 4] << 24) |
		       ((uint32_t)block[i * 4 + 1] << 16) |
		       ((uint32_t)block[i * 4 + 2] << 8) |
		       (uint32_t)block[i * 4 + 3];
	}
	for (int i = 16; i < 80; i++)
		w[i] = rol32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

	a = ctx->h[0];
	b = ctx->h[1];
	c = ctx->h[2];
	d = ctx->h[3];
	e = ctx->h[4];

	for (int i = 0; i < 80; i++) {
		uint32_t f, k;
		if (i < 20) {
			f = (b & c) | ((~b) & d);
			k = 0x5a827999u;
		} else if (i < 40) {
			f = b ^ c ^ d;
			k = 0x6ed9eba1u;
		} else if (i < 60) {
			f = (b & c) | (b & d) | (c & d);
			k = 0x8f1bbcdcu;
		} else {
			f = b ^ c ^ d;
			k = 0xca62c1d6u;
		}
		uint32_t t = rol32(a, 5) + f + e + k + w[i];
		e = d;
		d = c;
		c = rol32(b, 30);
		b = a;
		a = t;
	}

	ctx->h[0] += a;
	ctx->h[1] += b;
	ctx->h[2] += c;
	ctx->h[3] += d;
	ctx->h[4] += e;
}

static void sha1_update(struct sha1_ctx *ctx, const void *data, size_t len)
{
	const uint8_t *p = data;

	ctx->len += (uint64_t)len * 8;
	while (len > 0) {
		size_t n = sizeof(ctx->block) - ctx->used;
		if (n > len)
			n = len;
		memcpy(ctx->block + ctx->used, p, n);
		ctx->used += n;
		p += n;
		len -= n;
		if (ctx->used == sizeof(ctx->block)) {
			sha1_block(ctx, ctx->block);
			ctx->used = 0;
		}
	}
}

static void sha1_final(struct sha1_ctx *ctx, uint8_t out[20])
{
	uint64_t bits = ctx->len;

	ctx->block[ctx->used++] = 0x80;
	if (ctx->used > 56) {
		memset(ctx->block + ctx->used, 0, sizeof(ctx->block) - ctx->used);
		sha1_block(ctx, ctx->block);
		ctx->used = 0;
	}
	memset(ctx->block + ctx->used, 0, 56 - ctx->used);
	for (int i = 0; i < 8; i++)
		ctx->block[56 + i] = (uint8_t)(bits >> (56 - i * 8));
	sha1_block(ctx, ctx->block);

	for (int i = 0; i < 5; i++) {
		out[i * 4] = (uint8_t)(ctx->h[i] >> 24);
		out[i * 4 + 1] = (uint8_t)(ctx->h[i] >> 16);
		out[i * 4 + 2] = (uint8_t)(ctx->h[i] >> 8);
		out[i * 4 + 3] = (uint8_t)ctx->h[i];
	}
}

static void base64_encode(const uint8_t *in, size_t len, char *out)
{
	static const char alphabet[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	size_t i = 0, j = 0;

	while (i + 2 < len) {
		uint32_t triple = ((uint32_t)in[i] << 16) |
				  ((uint32_t)in[i + 1] << 8) |
				  (uint32_t)in[i + 2];
		out[j++] = alphabet[(triple >> 18) & 0x3f];
		out[j++] = alphabet[(triple >> 12) & 0x3f];
		out[j++] = alphabet[(triple >> 6) & 0x3f];
		out[j++] = alphabet[triple & 0x3f];
		i += 3;
	}
	if (i < len) {
		uint32_t triple = (uint32_t)in[i] << 16;
		out[j++] = alphabet[(triple >> 18) & 0x3f];
		if (i + 1 < len) {
			triple |= (uint32_t)in[i + 1] << 8;
			out[j++] = alphabet[(triple >> 12) & 0x3f];
			out[j++] = alphabet[(triple >> 6) & 0x3f];
			out[j++] = '=';
		} else {
			out[j++] = alphabet[(triple >> 12) & 0x3f];
			out[j++] = '=';
			out[j++] = '=';
		}
	}
	out[j] = '\0';
}

static char *find_header(char *req, const char *name)
{
	size_t name_len = strlen(name);
	char *line = req;

	while (line && *line) {
		char *next = strstr(line, "\r\n");
		if (next)
			*next = '\0';
		if (strncasecmp(line, name, name_len) == 0 && line[name_len] == ':') {
			char *v = line + name_len + 1;
			while (*v == ' ' || *v == '\t')
				v++;
			return v;
		}
		if (!next)
			break;
		line = next + 2;
	}
	return NULL;
}

static bool write_all(int fd, const void *buf, size_t len)
{
	const uint8_t *p = buf;

	while (len > 0) {
		ssize_t n = send(fd, p, len, MSG_NOSIGNAL);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			return false;
		}
		if (n == 0)
			return false;
		p += n;
		len -= (size_t)n;
	}
	return true;
}

static bool writev_all(int fd, struct iovec *iov, int iovcnt)
{
	while (iovcnt > 0) {
		ssize_t n = writev(fd, iov, iovcnt);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			return false;
		}
		if (n == 0)
			return false;

		while (iovcnt > 0 && (size_t)n >= iov[0].iov_len) {
			n -= (ssize_t)iov[0].iov_len;
			iov++;
			iovcnt--;
		}
		if (iovcnt > 0 && n > 0) {
			iov[0].iov_base = (uint8_t *)iov[0].iov_base + n;
			iov[0].iov_len -= (size_t)n;
		}
	}
	return true;
}

static bool websocket_handshake(int fd)
{
	char req[READ_BUF + 1];
	size_t used = 0;

	while (used < READ_BUF) {
		ssize_t n = recv(fd, req + used, READ_BUF - used, 0);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			return false;
		}
		if (n == 0)
			return false;
		used += (size_t)n;
		req[used] = '\0';
		if (strstr(req, "\r\n\r\n"))
			break;
	}

	char *key = find_header(req, "Sec-WebSocket-Key");
	if (!key)
		return false;

	char material[256];
	uint8_t digest[20];
	char accept[32];
	struct sha1_ctx sha;

	snprintf(material, sizeof(material), "%s%s", key, WS_GUID);
	sha1_init(&sha);
	sha1_update(&sha, material, strlen(material));
	sha1_final(&sha, digest);
	base64_encode(digest, sizeof(digest), accept);

	char resp[256];
	int len = snprintf(resp, sizeof(resp),
			   "HTTP/1.1 101 Switching Protocols\r\n"
			   "Upgrade: websocket\r\n"
			   "Connection: Upgrade\r\n"
			   "Sec-WebSocket-Accept: %s\r\n"
			   "\r\n",
			   accept);
	return len > 0 && (size_t)len < sizeof(resp) &&
	       write_all(fd, resp, (size_t)len);
}

static uint8_t xorshift32_byte(uint32_t *state)
{
	uint32_t x = *state;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*state = x;
	return (uint8_t)x;
}

static void fill_prbs(uint8_t *buf, size_t len, uint32_t *state)
{
	for (size_t i = 0; i < len; i++)
		buf[i] = xorshift32_byte(state);
}

static bool send_frame(int fd, const uint8_t *payload, size_t len)
{
	uint8_t hdr[10];
	size_t hdr_len;
	struct iovec iov[2];

	hdr[0] = 0x82;
	if (len <= 125) {
		hdr[1] = (uint8_t)len;
		hdr_len = 2;
	} else if (len <= 65535) {
		hdr[1] = 126;
		hdr[2] = (uint8_t)(len >> 8);
		hdr[3] = (uint8_t)len;
		hdr_len = 4;
	} else {
		uint64_t n = len;

		hdr[1] = 127;
		for (int i = 0; i < 8; i++)
			hdr[2 + i] = (uint8_t)(n >> (56 - i * 8));
		hdr_len = 10;
	}

	iov[0].iov_base = hdr;
	iov[0].iov_len = hdr_len;
	iov[1].iov_base = (void *)payload;
	iov[1].iov_len = len;
	return writev_all(fd, iov, 2);
}

static void send_close(int fd)
{
	uint8_t close_frame[2] = {0x88, 0x00};

	write_all(fd, close_frame, sizeof(close_frame));
}

static int listen_socket(int port)
{
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;

	int one = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons((uint16_t)port);

	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
	    listen(fd, 1) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static uint64_t parse_u64(const char *s)
{
	char *end = NULL;
	unsigned long long v = strtoull(s, &end, 0);

	if (!s[0] || (end && *end)) {
		fprintf(stderr, "bad integer: %s\n", s);
		exit(2);
	}
	return (uint64_t)v;
}

static void usage(const char *argv0)
{
	fprintf(stderr,
		"usage: %s [--port N] [--bytes N] [--seed N] "
		"[--frame-bytes N] [--sndbuf N] [--tcp-nodelay]\n"
		"Streams deterministic xorshift32 PRBS over one WebSocket client.\n",
		argv0);
}

int main(int argc, char **argv)
{
	int port = DEFAULT_PORT;
	uint64_t total = DEFAULT_BYTES;
	uint32_t seed = DEFAULT_SEED;
	size_t frame_bytes = FRAME_PAYLOAD;
	int sndbuf = 0;
	bool tcp_nodelay = false;
	static const struct option opts[] = {
		{"port", required_argument, NULL, 'p'},
		{"bytes", required_argument, NULL, 'b'},
		{"seed", required_argument, NULL, 's'},
		{"frame-bytes", required_argument, NULL, 'f'},
		{"sndbuf", required_argument, NULL, 'S'},
		{"tcp-nodelay", no_argument, NULL, 'n'},
		{"help", no_argument, NULL, 'h'},
		{NULL, 0, NULL, 0},
	};

	for (;;) {
		int c = getopt_long(argc, argv, "p:b:s:f:S:nh", opts, NULL);
		if (c < 0)
			break;
		switch (c) {
		case 'p':
			port = (int)parse_u64(optarg);
			break;
		case 'b':
			total = parse_u64(optarg);
			break;
		case 's':
			seed = (uint32_t)parse_u64(optarg);
			break;
		case 'f':
			frame_bytes = (size_t)parse_u64(optarg);
			break;
		case 'S':
			sndbuf = (int)parse_u64(optarg);
			break;
		case 'n':
			tcp_nodelay = true;
			break;
		case 'h':
			usage(argv[0]);
			return 0;
		default:
			usage(argv[0]);
			return 2;
		}
	}
	if (seed == 0)
		seed = 1;
	if (port <= 0 || port > 65535) {
		fprintf(stderr, "bad port: %d\n", port);
		return 2;
	}
	if (frame_bytes == 0 || frame_bytes > MAX_FRAME_PAYLOAD) {
		fprintf(stderr, "bad frame size: %zu\n", frame_bytes);
		return 2;
	}

	signal(SIGPIPE, SIG_IGN);

	int srv = listen_socket(port);
	if (srv < 0) {
		perror("listen");
		return 1;
	}
	printf("stream_ws_prbs listening port=%d bytes=%llu seed=0x%08x\n",
	       port, (unsigned long long)total, seed);
	fflush(stdout);

	int client = accept(srv, NULL, NULL);
	if (client < 0) {
		perror("accept");
		close(srv);
		return 1;
	}
	close(srv);
	if (sndbuf > 0)
		setsockopt(client, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
	if (tcp_nodelay) {
		int one = 1;
		setsockopt(client, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
	}

	if (!websocket_handshake(client)) {
		fprintf(stderr, "websocket handshake failed\n");
		close(client);
		return 1;
	}

	uint8_t *buf = malloc(frame_bytes);
	if (!buf) {
		perror("malloc");
		close(client);
		return 1;
	}

	uint64_t sent = 0;
	while (sent < total) {
		size_t n = frame_bytes;
		if (total - sent < n)
			n = (size_t)(total - sent);
		fill_prbs(buf, n, &seed);
		if (!send_frame(client, buf, n)) {
			perror("send");
			free(buf);
			close(client);
			return 1;
		}
		sent += n;
	}

	send_close(client);
	printf("stream_ws_prbs sent bytes=%llu\n", (unsigned long long)sent);
	free(buf);
	close(client);
	return 0;
}
