/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * lokictl-capture: the only part of LokiCTL that may listen to raw network
 * traffic. It only ever receives; it never sends a packet.
 *
 * LokiCTL runs as the normal user. Hearing the ARP, DHCP, mDNS and SSDP
 * traffic of the network (the Discover page) needs a packet socket, which
 * takes one Linux capability, CAP_NET_RAW. This small program holds it as a
 * file capability (set by the package's postinst; it is not setuid), and
 * only root and members of the netdev group may run it (root:netdev, mode
 * 0750).
 *
 * It does exactly one fixed command:
 *
 *   lokictl-capture discover INTERFACE SECONDS
 *
 * INTERFACE is the name of an existing network interface of Ethernet type
 * (a cable port, or Wi-Fi in its normal mode): 1 to 15 characters of a-z,
 * A-Z, 0-9, '.', '_' and '-'. It is looked up by the kernel; no file path
 * is ever made from it. SECONDS is 1 to CAPTURE_MAX_SECONDS.
 *
 * In order:
 * 1. A packet socket is opened with protocol 0, so it receives nothing yet.
 * 2. A fixed classic BPF filter is attached and locked: ARP, and IPv4 UDP
 *    that is not a fragment and either has both ports 67 or 68 (DHCP) or
 *    goes to port 5353 (mDNS) or 1900 (SSDP); every frame cut to
 *    CAPTURE_SNAP_LENGTH bytes. Nothing else ever reaches this program.
 *    No multicast group is joined: it hears only the mDNS and SSDP frames
 *    that reach the Cardputer anyway.
 * 3. The socket is bound to the interface. It is never put into
 *    promiscuous mode, so it hears only what reaches the Cardputer anyway.
 * 4. Every capability is dropped, no_new_privs is set, and a seccomp filter
 *    allows only what the rest needs: receiving, polling, the clock, write
 *    to standard output and standard error, close and exit. Every other
 *    call fails, so nothing can be sent from here on, not even by mistake.
 * 5. What it hears goes to standard output as a PCAP stream (link type
 *    Ethernet, microsecond times, this machine's byte order), and nowhere
 *    else: there is no file argument.
 *
 * It ends after SECONDS, after CAPTURE_MAX_BYTES of output, or as soon as
 * whoever reads its output has gone away.
 *
 * Exit status: 0 done (time up, size limit, or the reader went away);
 * 2 not the command, a bad interface or a bad number; 3 the capability is
 * missing; 4 the socket could not be set up, or failed.
 */

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <linux/audit.h>
#include <linux/capability.h>
#include <linux/filter.h>
#include <linux/if_ether.h>
#include <linux/seccomp.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <netpacket/packet.h>
#include <arpa/inet.h>
#include <poll.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

enum {
    EXIT_DONE = 0,
    EXIT_USAGE = 2,
    EXIT_NO_PERMISSION = 3,
    EXIT_SOCKET_FAILED = 4,
};

/* The longest part of a frame passed on. */
#define CAPTURE_SNAP_LENGTH 2048
/* The longest run, and the most output of one run. */
#define CAPTURE_MAX_SECONDS 1800
#define CAPTURE_MAX_BYTES (16UL * 1024UL * 1024UL)
/* At most this many frames are read per wake, so the clock is looked at
 * often enough even on a busy network. */
#define CAPTURE_MAX_READS 64
/* The PCAP file header and record header. */
#define PCAP_MAGIC 0xa1b2c3d4U
#define PCAP_LINK_ETHERNET 1U
#define PCAP_FILE_HEADER 24
#define PCAP_RECORD_HEADER 16

#if defined(__aarch64__)
#define CAPTURE_AUDIT_ARCH AUDIT_ARCH_AARCH64
#elif defined(__x86_64__)
#define CAPTURE_AUDIT_ARCH AUDIT_ARCH_X86_64
#else
#error "lokictl-capture: add this architecture's AUDIT_ARCH value"
#endif

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define SECCOMP_ARG0_LOW offsetof(struct seccomp_data, args[0])
#else
#define SECCOMP_ARG0_LOW (offsetof(struct seccomp_data, args[0]) + 4)
#endif

static const char kProgram[] = "lokictl-capture";

/* ARP; IPv4 UDP that is not a fragment and goes to port 5353 (mDNS) or
 * 1900 (SSDP), or whose source and destination port are each 67 or 68
 * (DHCP). Accepted frames are cut to CAPTURE_SNAP_LENGTH. A load past the
 * end of a frame drops it. */
static struct sock_filter kFrameFilter[] = {
    /* 0 */ BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 12),           /* EtherType */
    /* 1 */ BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, ETH_P_ARP, 15, 0), /* ARP: 17 */
    /* 2 */ BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, ETH_P_IP, 0, 15),  /* not IPv4: 18 */
    /* 3 */ BPF_STMT(BPF_LD | BPF_B | BPF_ABS, 23),           /* IPv4 protocol */
    /* 4 */ BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 17, 0, 13),   /* not UDP: 18 */
    /* 5 */ BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 20),           /* flags, fragment offset */
    /* 6 */ BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, 0x3fff, 11, 0), /* a fragment: 18 */
    /* 7 */ BPF_STMT(BPF_LDX | BPF_B | BPF_MSH, 14),          /* X = IPv4 header length */
    /* 8 */ BPF_STMT(BPF_LD | BPF_H | BPF_IND, 16),           /* UDP destination port */
    /* 9 */ BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 5353, 7, 0),  /* mDNS: 17 */
    /* 10 */ BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 1900, 6, 0), /* SSDP: 17 */
    /* 11 */ BPF_STMT(BPF_LD | BPF_H | BPF_IND, 14),          /* UDP source port */
    /* 12 */ BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 67, 1, 0),   /* 67: 14 */
    /* 13 */ BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 68, 0, 4),   /* not 68: 18 */
    /* 14 */ BPF_STMT(BPF_LD | BPF_H | BPF_IND, 16),          /* UDP destination port */
    /* 15 */ BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 67, 1, 0),   /* 67: 17 */
    /* 16 */ BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 68, 0, 1),   /* not 68: 18 */
    /* 17 */ BPF_STMT(BPF_RET | BPF_K, CAPTURE_SNAP_LENGTH),  /* accept */
    /* 18 */ BPF_STMT(BPF_RET | BPF_K, 0),                    /* drop */
};

/* One allowed system call: on a match it is allowed, otherwise the next
 * test follows. */
#define ALLOW_CALL(number)                                     \
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (number), 0, 1),       \
    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW)

/* The calls the capture loop makes once the socket is set up. Anything
 * else fails with EPERM; another architecture's calls end the program. */
static struct sock_filter kCallFilter[] = {
    BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch)),
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, CAPTURE_AUDIT_ARCH, 1, 0),
    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
    BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
    ALLOW_CALL(__NR_recvfrom),
    ALLOW_CALL(__NR_ppoll),
#ifdef __NR_poll
    ALLOW_CALL(__NR_poll),
#endif
    ALLOW_CALL(__NR_clock_gettime),
    ALLOW_CALL(__NR_gettimeofday),
    ALLOW_CALL(__NR_close),
    ALLOW_CALL(__NR_rt_sigreturn),
    ALLOW_CALL(__NR_exit),
    ALLOW_CALL(__NR_exit_group),
    /* write: only to standard output (1) and standard error (2). */
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_write, 0, 4),
    BPF_STMT(BPF_LD | BPF_W | BPF_ABS, SECCOMP_ARG0_LOW),
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 1, 1, 0),
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 2, 0, 1),
    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA)),
};

/* Writes a fixed message to standard error. Only write(2) is used, so it
 * works under the seccomp filter too. */
static void fail(const char* message) {
    char line[160];
    const int length = snprintf(line, sizeof(line), "%s: %s\n", kProgram, message);
    if (length > 0) {
        const size_t size = (size_t)length < sizeof(line) ? (size_t)length : sizeof(line) - 1;
        ssize_t ignored = write(2, line, size);
        (void)ignored;
    }
}

static void usage(void) {
    fprintf(stderr,
            "usage: %s discover INTERFACE SECONDS\n"
            "  INTERFACE: an Ethernet-type interface, for example eth0 or wlan0\n"
            "  SECONDS: 1 to %d\n",
            kProgram,
            CAPTURE_MAX_SECONDS);
}

/* Closes every descriptor above 2 and makes sure 0, 1 and 2 are open, so
 * nothing the caller left open stays here, and the stream lands only where
 * standard output points. */
static void tidy_descriptors(void) {
#ifdef SYS_close_range
    if (syscall(SYS_close_range, 3U, ~0U, 0U) != 0)
#endif
    {
        long last = sysconf(_SC_OPEN_MAX);
        if (last < 0 || last > 65536) {
            last = 65536;
        }
        for (long fd = 3; fd < last; ++fd) {
            close((int)fd);
        }
    }
    for (int fd = 0; fd <= 2; ++fd) {
        if (fcntl(fd, F_GETFD) == -1 && errno == EBADF) {
            /* Takes the lowest free descriptor, which is fd. */
            if (open("/dev/null", O_RDWR) != fd) {
                _exit(EXIT_SOCKET_FAILED);
            }
        }
    }
}

/* 1 to 15 characters of a-z, A-Z, 0-9, '.', '_' and '-', not "." or "..". */
static int valid_interface_name(const char* name) {
    const size_t length = strlen(name);
    if (length == 0 || length >= IFNAMSIZ || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return 0;
    }
    for (size_t index = 0; index < length; ++index) {
        const char c = name[index];
        const int allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                            c == '.' || c == '_' || c == '-';
        if (!allowed) {
            return 0;
        }
    }
    return 1;
}

/* A whole number of 1 to CAPTURE_MAX_SECONDS, digits only, no leading zero.
 * Returns it, or 0 for anything else. */
static int parse_seconds(const char* text) {
    const size_t length = strlen(text);
    if (length == 0 || length > 4 || text[0] == '0') {
        return 0;
    }
    int value = 0;
    for (size_t index = 0; index < length; ++index) {
        if (text[index] < '0' || text[index] > '9') {
            return 0;
        }
        value = value * 10 + (text[index] - '0');
    }
    return value <= CAPTURE_MAX_SECONDS ? value : 0;
}

/* Whether CAP_NET_RAW is ours. */
static int has_capability(void) {
    struct __user_cap_header_struct header;
    struct __user_cap_data_struct data[_LINUX_CAPABILITY_U32S_3];
    memset(&header, 0, sizeof(header));
    memset(data, 0, sizeof(data));
    header.version = _LINUX_CAPABILITY_VERSION_3;
    if (syscall(SYS_capget, &header, data) != 0) {
        return 0;
    }
    const __u32 bit = (__u32)1 << (CAP_NET_RAW % 32);
    return (data[CAP_NET_RAW / 32].effective & bit) != 0;
}

/* Drops every capability, for good: none effective, permitted or
 * inheritable, none ambient, and no way to gain any (no_new_privs). */
static int drop_capabilities(void) {
    struct __user_cap_header_struct header;
    struct __user_cap_data_struct data[_LINUX_CAPABILITY_U32S_3];
    memset(&header, 0, sizeof(header));
    memset(data, 0, sizeof(data));
    header.version = _LINUX_CAPABILITY_VERSION_3;
    if (syscall(SYS_capset, &header, data) != 0) {
        return -1;
    }
    if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_CLEAR_ALL, 0, 0, 0) != 0 && errno != EINVAL) {
        return -1;
    }
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
        return -1;
    }
    return 0;
}

static int lock_calls(void) {
    struct sock_fprog program = {
        .len = (unsigned short)(sizeof(kCallFilter) / sizeof(kCallFilter[0])),
        .filter = kCallFilter,
    };
    return prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program, 0, 0);
}

/* Opens the packet socket on `name`, with the filter locked on it, bound
 * and receiving. Exits with EXIT_USAGE, EXIT_NO_PERMISSION or
 * EXIT_SOCKET_FAILED when it cannot. */
static int open_socket(const char* name) {
    const unsigned int index = if_nametoindex(name);
    if (index == 0) {
        fail("no such interface");
        _exit(EXIT_USAGE);
    }
    /* Protocol 0: nothing is received before bind(), so no frame gets in
     * past the filter. */
    const int fd = socket(AF_PACKET, SOCK_RAW | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        if (errno == EPERM || errno == EACCES) {
            fail("missing the raw-network capability (reinstall the lokictl package)");
            _exit(EXIT_NO_PERMISSION);
        }
        fail("could not open a packet socket");
        _exit(EXIT_SOCKET_FAILED);
    }

    struct ifreq request;
    memset(&request, 0, sizeof(request));
    memcpy(request.ifr_name, name, strlen(name));
    if (ioctl(fd, SIOCGIFHWADDR, &request) != 0) {
        fail("no such interface");
        _exit(EXIT_USAGE);
    }
    if (request.ifr_hwaddr.sa_family != ARPHRD_ETHER) {
        fail("not an Ethernet-type interface");
        _exit(EXIT_USAGE);
    }

    struct sock_fprog filter = {
        .len = (unsigned short)(sizeof(kFrameFilter) / sizeof(kFrameFilter[0])),
        .filter = kFrameFilter,
    };
    const int lock = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_ATTACH_FILTER, &filter, sizeof(filter)) != 0 ||
        setsockopt(fd, SOL_SOCKET, SO_LOCK_FILTER, &lock, sizeof(lock)) != 0) {
        fail("could not set the frame filter");
        _exit(EXIT_SOCKET_FAILED);
    }

    struct sockaddr_ll address;
    memset(&address, 0, sizeof(address));
    address.sll_family = AF_PACKET;
    address.sll_protocol = htons(ETH_P_ALL);
    address.sll_ifindex = (int)index;
    if (bind(fd, (const struct sockaddr*)&address, sizeof(address)) != 0) {
        fail("could not listen on the interface");
        _exit(EXIT_SOCKET_FAILED);
    }
    return fd;
}

static void put32(unsigned char* out, uint32_t value) {
    memcpy(out, &value, sizeof(value));
}

/* Writes all of `data` to standard output. Returns 0, or -1 when the
 * reader has gone away (or the write failed). */
static int write_all(const unsigned char* data, size_t size) {
    while (size > 0) {
        const ssize_t written = write(1, data, size);
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            return -1;
        }
        data += written;
        size -= (size_t)written;
    }
    return 0;
}

static int64_t now_ms(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

int main(int argc, char* argv[]) {
    tidy_descriptors();
    /* A reader that went away shows as a failed write, not a signal. A
     * caller's ignored SIGTERM or SIGALRM must not keep this running. */
    signal(SIGPIPE, SIG_IGN);
    signal(SIGTERM, SIG_DFL);
    signal(SIGALRM, SIG_DFL);

    /* Every argument is checked before anything is opened. */
    if (argc != 4 || strcmp(argv[1], "discover") != 0) {
        usage();
        return EXIT_USAGE;
    }
    const char* name = argv[2];
    if (!valid_interface_name(name)) {
        fail("not an interface name");
        return EXIT_USAGE;
    }
    const int seconds = parse_seconds(argv[3]);
    if (seconds == 0) {
        fail("seconds must be a whole number from 1 to 1800");
        return EXIT_USAGE;
    }
    if (!has_capability()) {
        fail("missing the raw-network capability (reinstall the lokictl package)");
        return EXIT_NO_PERMISSION;
    }

    const int fd = open_socket(name);
    /* A last stop, should the loop below ever be stuck in a write (set
     * before the seccomp filter, which does not allow it). */
    alarm((unsigned int)seconds + 5U);
    if (drop_capabilities() != 0 || lock_calls() != 0) {
        fail("could not drop its rights");
        _exit(EXIT_SOCKET_FAILED);
    }

    /* From here on only the calls kCallFilter allows. */
    unsigned char header[PCAP_FILE_HEADER];
    memset(header, 0, sizeof(header));
    const uint16_t version[2] = {2, 4};
    put32(header, PCAP_MAGIC);
    memcpy(header + 4, version, sizeof(version));
    put32(header + 16, CAPTURE_SNAP_LENGTH);
    put32(header + 20, PCAP_LINK_ETHERNET);
    if (write_all(header, sizeof(header)) != 0) {
        _exit(EXIT_DONE);
    }

    static unsigned char record[PCAP_RECORD_HEADER + CAPTURE_SNAP_LENGTH];
    unsigned long written = PCAP_FILE_HEADER;
    const int64_t deadline = now_ms() + (int64_t)seconds * 1000;
    for (;;) {
        const int64_t left = deadline - now_ms();
        if (left <= 0) {
            _exit(EXIT_DONE);
        }
        /* Standard output is watched too: its reader closing shows as an
         * error on it, even when no frame comes. */
        struct pollfd entries[2] = {{fd, POLLIN, 0}, {1, 0, 0}};
        const int ready = poll(entries, 2, left < 1000 ? (int)left : 1000);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            _exit(EXIT_SOCKET_FAILED);
        }
        if (entries[1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            _exit(EXIT_DONE);
        }
        if (entries[0].revents & (POLLERR | POLLNVAL)) {
            fail("the interface went away");
            _exit(EXIT_SOCKET_FAILED);
        }
        if (!(entries[0].revents & POLLIN)) {
            continue;
        }
        for (int reads = 0; reads < CAPTURE_MAX_READS; ++reads) {
            const ssize_t got = recvfrom(fd, record + PCAP_RECORD_HEADER, CAPTURE_SNAP_LENGTH,
                                         MSG_DONTWAIT | MSG_TRUNC, NULL, NULL);
            if (got < 0 && errno == EINTR) {
                continue;
            }
            if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                break;
            }
            if (got < 0) {
                fail("the interface went away");
                _exit(EXIT_SOCKET_FAILED);
            }
            const uint32_t length = (uint32_t)got;
            const uint32_t kept = length < CAPTURE_SNAP_LENGTH ? length : CAPTURE_SNAP_LENGTH;
            struct timespec now;
            clock_gettime(CLOCK_REALTIME, &now);
            put32(record, (uint32_t)now.tv_sec);
            put32(record + 4, (uint32_t)(now.tv_nsec / 1000));
            put32(record + 8, kept);
            put32(record + 12, length);
            if (write_all(record, PCAP_RECORD_HEADER + kept) != 0) {
                _exit(EXIT_DONE);
            }
            written += PCAP_RECORD_HEADER + kept;
            if (written >= CAPTURE_MAX_BYTES) {
                _exit(EXIT_DONE);
            }
        }
    }
}
