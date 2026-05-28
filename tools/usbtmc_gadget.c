// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jakob Kastelic
// usbtmc_gadget.c --- minimal USBTMC/USB488 FunctionFS instrument

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <linux/usb/ch9.h>
#include <linux/usb/functionfs.h>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define FFS_DIR "/dev/ffs-usbtmc"
#define READY_FILE "/run/usbtmc_gadget.ready"
#define MAX_OUT (64 * 1024)
#define MAX_RESP (1024 * 1024)
#define MAX_IN_FRAME (12 + MAX_RESP + 3)

#define USBTMC_MSGID_DEV_DEP_MSG_OUT 1
#define USBTMC_MSGID_REQUEST_DEV_DEP_MSG_IN 2
#define USBTMC_STATUS_SUCCESS 1
#define USBTMC_STATUS_FAILED 0x80

static int ep0 = -1;
static int ep_out = -1;
static int ep_in = -1;
static int ep_intr = -1;
static uint8_t bulk_out[MAX_OUT];
static uint8_t response[MAX_RESP];
static uint8_t in_frame[MAX_IN_FRAME];
static size_t response_len;
static uint8_t response_tag;
static unsigned ese;
static unsigned sre;
static unsigned esr;
static char last_error[128] = "0,\"No error\"";

struct __attribute__((packed)) descriptors {
    struct usb_functionfs_descs_head_v2 header;
    uint32_t fs_count;
    uint32_t hs_count;
    struct {
        struct usb_interface_descriptor intf;
        struct usb_endpoint_descriptor_no_audio bulk_out;
        struct usb_endpoint_descriptor_no_audio bulk_in;
        struct usb_endpoint_descriptor_no_audio intr_in;
    } fs;
    struct {
        struct usb_interface_descriptor intf;
        struct usb_endpoint_descriptor_no_audio bulk_out;
        struct usb_endpoint_descriptor_no_audio bulk_in;
        struct usb_endpoint_descriptor_no_audio intr_in;
    } hs;
};

struct __attribute__((packed)) strings {
    struct usb_functionfs_strings_head header;
    uint16_t lang;
    char intf[sizeof("USBTMC USB488")];
};

static uint32_t xorshift32(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static void write_all_or_die(int fd, const void *buf, size_t len,
                             const char *what)
{
    const uint8_t *p = buf;
    while (len) {
        ssize_t n = write(fd, p, len);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            perror(what);
            exit(1);
        }
        p += n;
        len -= (size_t)n;
    }
}

static int write_all_timeout(int fd, const void *buf, size_t len)
{
    const uint8_t *p = buf;
    while (len) {
        ssize_t n = write(fd, p, len);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                struct pollfd pfd = {.fd = fd, .events = POLLOUT};
                int rc;
                do {
                    rc = poll(&pfd, 1, 5000);
                } while (rc < 0 && errno == EINTR);
                if (rc > 0)
                    continue;
            }
            return -1;
        }
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

static void write_descriptors(void)
{
    struct descriptors d = {
        .header = {
            .magic = FUNCTIONFS_DESCRIPTORS_MAGIC_V2,
            .length = sizeof(d),
            .flags = FUNCTIONFS_HAS_FS_DESC | FUNCTIONFS_HAS_HS_DESC,
        },
        .fs_count = 4,
        .hs_count = 4,
        .fs = {
            .intf = {
                .bLength = sizeof(struct usb_interface_descriptor),
                .bDescriptorType = USB_DT_INTERFACE,
                .bInterfaceNumber = 0,
                .bAlternateSetting = 0,
                .bNumEndpoints = 3,
                .bInterfaceClass = 0xfe,
                .bInterfaceSubClass = 0x03,
                .bInterfaceProtocol = 0x01,
                .iInterface = 1,
            },
            .bulk_out = {
                .bLength = sizeof(struct usb_endpoint_descriptor_no_audio),
                .bDescriptorType = USB_DT_ENDPOINT,
                .bEndpointAddress = USB_DIR_OUT | 1,
                .bmAttributes = USB_ENDPOINT_XFER_BULK,
                .wMaxPacketSize = 64,
                .bInterval = 0,
            },
            .bulk_in = {
                .bLength = sizeof(struct usb_endpoint_descriptor_no_audio),
                .bDescriptorType = USB_DT_ENDPOINT,
                .bEndpointAddress = USB_DIR_IN | 2,
                .bmAttributes = USB_ENDPOINT_XFER_BULK,
                .wMaxPacketSize = 64,
                .bInterval = 0,
            },
            .intr_in = {
                .bLength = sizeof(struct usb_endpoint_descriptor_no_audio),
                .bDescriptorType = USB_DT_ENDPOINT,
                .bEndpointAddress = USB_DIR_IN | 3,
                .bmAttributes = USB_ENDPOINT_XFER_INT,
                .wMaxPacketSize = 2,
                .bInterval = 16,
            },
        },
        .hs = {
            .intf = {
                .bLength = sizeof(struct usb_interface_descriptor),
                .bDescriptorType = USB_DT_INTERFACE,
                .bInterfaceNumber = 0,
                .bAlternateSetting = 0,
                .bNumEndpoints = 3,
                .bInterfaceClass = 0xfe,
                .bInterfaceSubClass = 0x03,
                .bInterfaceProtocol = 0x01,
                .iInterface = 1,
            },
            .bulk_out = {
                .bLength = sizeof(struct usb_endpoint_descriptor_no_audio),
                .bDescriptorType = USB_DT_ENDPOINT,
                .bEndpointAddress = USB_DIR_OUT | 1,
                .bmAttributes = USB_ENDPOINT_XFER_BULK,
                .wMaxPacketSize = 512,
                .bInterval = 0,
            },
            .bulk_in = {
                .bLength = sizeof(struct usb_endpoint_descriptor_no_audio),
                .bDescriptorType = USB_DT_ENDPOINT,
                .bEndpointAddress = USB_DIR_IN | 2,
                .bmAttributes = USB_ENDPOINT_XFER_BULK,
                .wMaxPacketSize = 512,
                .bInterval = 0,
            },
            .intr_in = {
                .bLength = sizeof(struct usb_endpoint_descriptor_no_audio),
                .bDescriptorType = USB_DT_ENDPOINT,
                .bEndpointAddress = USB_DIR_IN | 3,
                .bmAttributes = USB_ENDPOINT_XFER_INT,
                .wMaxPacketSize = 2,
                .bInterval = 8,
            },
        },
    };
    struct strings s = {
        .header = {
            .magic = FUNCTIONFS_STRINGS_MAGIC,
            .length = sizeof(s),
            .str_count = 1,
            .lang_count = 1,
        },
        .lang = 0x0409,
        .intf = "USBTMC USB488",
    };

    write_all_or_die(ep0, &d, sizeof(d), "write descriptors");
    write_all_or_die(ep0, &s, sizeof(s), "write strings");
}

static void set_error(const char *cmd)
{
    snprintf(last_error, sizeof(last_error), "-113,\"Undefined header: %.48s\"",
             cmd);
    esr |= 0x20;
}

static void set_response_text(const char *s)
{
    response_len = strlen(s);
    if (response_len > sizeof(response))
        response_len = sizeof(response);
    memcpy(response, s, response_len);
}

static void set_response_prbs(size_t n, uint32_t seed)
{
    uint32_t state = seed;
    response_len = n > sizeof(response) ? sizeof(response) : n;
    for (size_t i = 0; i < response_len; i++) {
        if ((i & 3) == 0)
            xorshift32(&state);
        response[i] = (uint8_t)(state >> (8 * (i & 3)));
    }
}

static void handle_scpi(char *cmd)
{
    char *p = cmd;
    while (*p == ' ' || *p == '\t')
        p++;
    char *e = p + strlen(p);
    while (e > p && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' ||
                     e[-1] == '\t'))
        *--e = '\0';

    if (!strcmp(p, "*IDN?")) {
        set_response_text("STMicroelectronics,STM32MP135-USBTMC,0001,1.0\n");
    } else if (!strcmp(p, "*CLS")) {
        esr = 0;
        strcpy(last_error, "0,\"No error\"");
        response_len = 0;
    } else if (!strncmp(p, "*ESE ", 5)) {
        ese = (unsigned)strtoul(p + 5, NULL, 0) & 0xff;
        response_len = 0;
    } else if (!strcmp(p, "*ESE?")) {
        snprintf((char *)response, sizeof(response), "%u\n", ese);
        response_len = strlen((char *)response);
    } else if (!strcmp(p, "*ESR?")) {
        snprintf((char *)response, sizeof(response), "%u\n", esr & 0xff);
        response_len = strlen((char *)response);
        esr = 0;
    } else if (!strcmp(p, "*RST")) {
        ese = 0;
        sre = 0;
        esr = 0;
        strcpy(last_error, "0,\"No error\"");
        response_len = 0;
    } else if (!strcmp(p, "*OPC")) {
        esr |= 1;
        response_len = 0;
    } else if (!strcmp(p, "*OPC?")) {
        set_response_text("1\n");
    } else if (!strncmp(p, "*SRE ", 5)) {
        sre = (unsigned)strtoul(p + 5, NULL, 0) & 0xff;
        response_len = 0;
    } else if (!strcmp(p, "*SRE?")) {
        snprintf((char *)response, sizeof(response), "%u\n", sre);
        response_len = strlen((char *)response);
    } else if (!strcmp(p, "*STB?")) {
        set_response_text("0\n");
    } else if (!strcmp(p, "*TRG")) {
        response_len = 0;
    } else if (!strcmp(p, "*TST?")) {
        set_response_text("0\n");
    } else if (!strcmp(p, "*WAI")) {
        response_len = 0;
    } else if (!strcmp(p, "SYST:ERR?")) {
        snprintf((char *)response, sizeof(response), "%s\n", last_error);
        response_len = strlen((char *)response);
        strcpy(last_error, "0,\"No error\"");
    } else if (!strncmp(p, "DATA:PRBS? ", 11)) {
        unsigned long n = strtoul(p + 11, NULL, 0);
        set_response_prbs(n, 0x12345678);
    } else {
        set_error(p);
        response_len = 0;
    }
}

static void handle_bulk_out(void)
{
    ssize_t n = read(ep_out, bulk_out, sizeof(bulk_out));
    if (n < 0) {
        if (errno != EINTR && errno != EAGAIN)
            perror("read ep_out");
        return;
    }
    if (n < 12)
        return;

    uint8_t msg = bulk_out[0];
    uint8_t tag = bulk_out[1];
    if ((uint8_t)~tag != bulk_out[2])
        return;

    if (msg == USBTMC_MSGID_DEV_DEP_MSG_OUT) {
        uint32_t len = (uint32_t)bulk_out[4] |
                       ((uint32_t)bulk_out[5] << 8) |
                       ((uint32_t)bulk_out[6] << 16) |
                       ((uint32_t)bulk_out[7] << 24);
        if (len > (uint32_t)n - 12)
            len = (uint32_t)n - 12;
        if (len >= MAX_OUT - 12)
            len = MAX_OUT - 13;
        char cmd[MAX_OUT];
        memcpy(cmd, bulk_out + 12, len);
        cmd[len] = '\0';
        response_tag = tag;
        handle_scpi(cmd);
    } else if (msg == USBTMC_MSGID_REQUEST_DEV_DEP_MSG_IN) {
        uint32_t want = (uint32_t)bulk_out[4] |
                        ((uint32_t)bulk_out[5] << 8) |
                        ((uint32_t)bulk_out[6] << 16) |
                        ((uint32_t)bulk_out[7] << 24);
        size_t nresp = response_len < want ? response_len : want;
        size_t pad = (4 - (nresp & 3)) & 3;
        uint8_t hdr[12] = {
            USBTMC_MSGID_REQUEST_DEV_DEP_MSG_IN,
            tag,
            (uint8_t)~tag,
            0,
            (uint8_t)(nresp & 0xff),
            (uint8_t)((nresp >> 8) & 0xff),
            (uint8_t)((nresp >> 16) & 0xff),
            (uint8_t)((nresp >> 24) & 0xff),
            1,
            0,
            0,
            0,
        };
        (void)response_tag;
        memcpy(in_frame, hdr, sizeof(hdr));
        if (nresp)
            memcpy(in_frame + sizeof(hdr), response, nresp);
        if (pad) {
            static const uint8_t zero[4];
            memcpy(in_frame + sizeof(hdr) + nresp, zero, pad);
        }
        if (write_all_timeout(ep_in, in_frame, sizeof(hdr) + nresp + pad) < 0)
            return;
    }
}

static void handle_setup(const struct usb_ctrlrequest *s)
{
    uint8_t type = s->bRequestType;
    uint8_t req = s->bRequest;
    uint8_t data[24] = {0};

    if ((type & USB_TYPE_MASK) != USB_TYPE_CLASS ||
        (type & USB_RECIP_MASK) != USB_RECIP_INTERFACE) {
        if (type & USB_DIR_IN)
            (void)write(ep0, NULL, 0);
        else
            (void)read(ep0, data, 0);
        return;
    }

    if ((type & USB_DIR_IN) && req == 7) {
        data[0] = USBTMC_STATUS_SUCCESS;
        data[2] = 0x00;
        data[3] = 0x01;
        data[4] = 0x03;
        data[5] = 0x00;
        data[12] = 0x00;
        data[13] = 0x01;
        data[14] = 0x0d;
        data[15] = 0x05;
        size_t n = s->wLength < sizeof(data) ? s->wLength : sizeof(data);
        write_all_or_die(ep0, data, n, "GET_CAPABILITIES");
    } else if (type & USB_DIR_IN) {
        data[0] = USBTMC_STATUS_SUCCESS;
        write_all_or_die(ep0, data, s->wLength ? 1 : 0, "class IN");
    } else {
        (void)read(ep0, data, 0);
    }
}

static void open_eps(void)
{
    ep_out = open(FFS_DIR "/ep1", O_RDONLY | O_NONBLOCK);
    ep_in = open(FFS_DIR "/ep2", O_WRONLY | O_NONBLOCK);
    ep_intr = open(FFS_DIR "/ep3", O_WRONLY | O_NONBLOCK);
    if (ep_out < 0 || ep_in < 0 || ep_intr < 0) {
        perror("open endpoints");
        exit(1);
    }
}

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    mkdir("/run", 0755);
    unlink(READY_FILE);

    ep0 = open(FFS_DIR "/ep0", O_RDWR);
    if (ep0 < 0) {
        perror("open ep0");
        return 1;
    }

    write_descriptors();
    puts("usbtmc_gadget: descriptors written");
    int ready = open(READY_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (ready >= 0) {
        write_all_or_die(ready, "ready\n", 6, "ready");
        close(ready);
        puts("usbtmc_gadget: ready");
    } else {
        perror("ready");
        return 1;
    }

    for (;;) {
        struct pollfd pfd[2] = {
            {.fd = ep0, .events = POLLIN},
            {.fd = ep_out, .events = POLLIN},
        };
        int nfds = ep_out >= 0 ? 2 : 1;
        int rc = poll(pfd, nfds, -1);
        if (rc < 0) {
            if (errno == EINTR)
                continue;
            perror("poll");
            return 1;
        }
        if (pfd[0].revents & POLLIN) {
            struct usb_functionfs_event ev[4];
            ssize_t n = read(ep0, ev, sizeof(ev));
            if (n < 0)
                continue;
            for (ssize_t off = 0; off + (ssize_t)sizeof(ev[0]) <= n;
                 off += sizeof(ev[0])) {
                struct usb_functionfs_event *e =
                    (struct usb_functionfs_event *)((uint8_t *)ev + off);
                if (e->type == FUNCTIONFS_ENABLE && ep_out < 0)
                    open_eps();
                else if (e->type == FUNCTIONFS_SETUP)
                    handle_setup(&e->u.setup);
                else if (e->type == FUNCTIONFS_DISABLE && ep_out >= 0) {
                    close(ep_out);
                    close(ep_in);
                    close(ep_intr);
                    ep_out = ep_in = ep_intr = -1;
                }
            }
        }
        if (ep_out >= 0 && (pfd[1].revents & POLLIN))
            handle_bulk_out();
    }
}
