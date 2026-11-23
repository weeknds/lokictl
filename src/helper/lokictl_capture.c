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
 * traffic of the network (the Discover page), what Watch looks for, and
 * what switches say about themselves (the Map's topology), needs a packet
 * socket, which takes one Linux capability, CAP_NET_RAW. This small
 * program holds it as a
 * file capability (set by the package's postinst; it is not setuid), and
 * only root and members of the netdev group may run it (root:netdev, mode
 * 0750).
 *
 * It does one of three fixed commands, which differ only in their frame
 * filter (and, for topology, the two link-layer groups it listens to):
 *
 *   lokictl-capture discover INTERFACE SECONDS
 *   lokictl-capture watch INTERFACE SECONDS
 *   lokictl-capture topology INTERFACE SECONDS
 *
 * INTERFACE is the name of an existing network interface of Ethernet type
 * (a cable port, or Wi-Fi in its normal mode): 1 to 15 characters of a-z,
 * A-Z, 0-9, '.', '_' and '-'. It is looked up by the kernel; no file path
 * is ever made from it. SECONDS is 1 to CAPTURE_MAX_SECONDS.
 *
 * In order:
 * 1. A packet socket is opened with protocol 0, so it receives nothing yet.
 * 2. A fixed classic BPF filter is attached and locked; every frame is cut
 *    to CAPTURE_SNAP_LENGTH bytes, and nothing else ever reaches this
 *    program.
 *    discover: ARP, and IPv4 UDP that is not a fragment and either has
 *    both ports 67 or 68 (DHCP) or goes to port 5353 (mDNS) or 1900
 *    (SSDP). No multicast group is joined: it hears only the mDNS and SSDP
 *    frames that reach the Cardputer anyway.
 *    watch: ARP; IPv4 UDP that is not a fragment and has both ports 67 or
 *    68 (DHCP); IPv4 TCP that is not a fragment, asks for a connection (SYN
 *    set, ACK clear) and was sent to this machine (packet type "host", so
 *    not its own outgoing ones); and IPv6 router announcements (ICMPv6
 *    type 134 right after the IPv6 header).
 *    topology: LLDP (EtherType 0x88cc sent to 01:80:c2:00:00:0e) and CDP
 *    (an IEEE 802.3 frame sent to 01:00:0c:cc:cc:cc with LLC SNAP, the
 *    organisation code 00:00:0c and protocol 0x2000), the frames in which
 *    switches, access points and routers say their name and port.
 * 3. The socket is bound to the interface. It is never put into
 *    promiscuous mode, so it hears only what reaches the Cardputer anyway.
 *    For topology only, the socket also listens to those two link-layer
 *    groups (PACKET_ADD_MEMBERSHIP), since a network card passes such
 *    frames on only when asked to. This sends nothing and changes no
 *    setting: the kernel drops both again when the socket closes, at the
 *    latest when this program ends. discover and watch join no group.
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

/* discover: ARP; IPv4 UDP that is not a fragment and goes to port 5353
 * (mDNS) or 1900 (SSDP), or whose source and destination port are each 67
 * or 68 (DHCP). Accepted frames are cut to CAPTURE_SNAP_LENGTH. A load past
 * the end of a frame drops it. */
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

/* watch: ARP; IPv4 UDP that is not a fragment and whose source and
 * destination port are each 67 or 68 (DHCP); IPv4 TCP that is not a
 * fragment, has SYN set and ACK clear, and came to this machine (packet
 * type PACKET_HOST); IPv6 whose next header is ICMPv6 with type 134 (a
 * router announcement). Accepted frames are cut to CAPTURE_SNAP_LENGTH. A
 * load past the end of a frame drops it. */
static struct sock_filter kWatchFilter[] = {
    /* 0 */ BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 12),           /* EtherType */
    /* 1 */ BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, ETH_P_ARP, 24, 0), /* ARP: 26 */
    /* 2 */ BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, ETH_P_IP, 0, 18),  /* not IPv4: 21 */
    /* 3 */ BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 20),           /* flags, fragment offset */
    /* 4 */ BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, 0x3fff, 22, 0), /* a fragment: 27 */
    /* 5 */ BPF_STMT(BPF_LD | BPF_B | BPF_ABS, 23),           /* IPv4 protocol */
    /* 6 */ BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 17, 0, 7),    /* not UDP: 14 */
    /* 7 */ BPF_STMT(BPF_LDX | BPF_B | BPF_MSH, 14),          /* X = IPv4 header length */
    /* 8 */ BPF_STMT(BPF_LD | BPF_H | BPF_IND, 14),           /* UDP source port */
    /* 9 */ BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 67, 1, 0),    /* 67: 11 */
    /* 10 */ BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 68, 0, 16),  /* not 68: 27 */
    /* 11 */ BPF_STMT(BPF_LD | BPF_H | BPF_IND, 16),          /* UDP destination port */
    /* 12 */ BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 67, 13, 0),  /* 67: 26 */
    /* 13 */ BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 68, 12, 13), /* 68: 26, else 27 */
    /* 14 */ BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 6, 0, 12),   /* not TCP: 27 */
    /* 15 */ BPF_STMT(BPF_LD | BPF_B | BPF_ABS, (uint32_t)(SKF_AD_OFF + SKF_AD_PKTTYPE)), /* packet type */
    /* 16 */ BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, PACKET_HOST, 0, 10), /* not to this machine: 27 */
    /* 17 */ BPF_STMT(BPF_LDX | BPF_B | BPF_MSH, 14),         /* X = IPv4 header length */
    /* 18 */ BPF_STMT(BPF_LD | BPF_B | BPF_IND, 27),          /* TCP flags (14 + X + 13) */
    /* 19 */ BPF_STMT(BPF_ALU | BPF_AND | BPF_K, 0x12),       /* SYN and ACK */
    /* 20 */ BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0x02, 5, 6), /* SYN only: 26, else 27 */
    /* 21 */ BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, ETH_P_IPV6, 0, 5), /* not IPv6: 27 */
    /* 22 */ BPF_STMT(BPF_LD | BPF_B | BPF_ABS, 20),          /* IPv6 next header */
    /* 23 */ BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 58, 0, 3),   /* not ICMPv6: 27 */
    /* 24 */ BPF_STMT(BPF_LD | BPF_B | BPF_ABS, 54),          /* ICMPv6 type */
    /* 25 */ BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 134, 0, 1),  /* router announcement: 26, else 27 */
    /* 26 */ BPF_STMT(BPF_RET | BPF_K, CAPTURE_SNAP_LENGTH),  /* accept */
    /* 27 */ BPF_STMT(BPF_RET | BPF_K, 0),                    /* drop */
};

/* topology: LLDP sent to the nearest-bridge group 01:80:c2:00:00:0e, and
 * CDP: an IEEE 802.3 frame (a length, not an EtherType, at byte 12) sent
 * to 01:00:0c:cc:cc:cc with LLC SNAP aa aa 03, the organisation code
 * 00:00:0c and protocol 0x2000 (other Cisco protocols that share the group,
 * such as VTP and DTP, are dropped). Accepted frames are cut to
 * CAPTURE_SNAP_LENGTH. A load past the end of a frame drops it. */
static struct sock_filter kTopologyFilter[] = {
    /* 0 */ BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 12),             /* EtherType, or an 802.3 length */
    /* 1 */ BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0x88cc, 0, 4),   /* not LLDP: 6 */
    /* 2 */ BPF_STMT(BPF_LD | BPF_W | BPF_ABS, 0),              /* destination, bytes 0-3 */
    /* 3 */ BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0x0180c200, 0, 12), /* not the LLDP group: 16 */
    /* 4 */ BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 4),              /* destination, bytes 4-5 */
    /* 5 */ BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0x000e, 9, 10),  /* the LLDP group: 15, else 16 */
    /* 6 */ BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K, 0x0600, 9, 0),   /* an EtherType, not a length: 16 */
    /* 7 */ BPF_STMT(BPF_LD | BPF_W | BPF_ABS, 0),              /* destination, bytes 0-3 */
    /* 8 */ BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0x01000ccc, 0, 7), /* not the CDP group: 16 */
    /* 9 */ BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 4),              /* destination, bytes 4-5 */
    /* 10 */ BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0xcccc, 0, 5),  /* not the CDP group: 16 */
    /* 11 */ BPF_STMT(BPF_LD | BPF_W | BPF_ABS, 14),            /* LLC aa aa 03, organisation code byte 1 */
    /* 12 */ BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0xaaaa0300, 0, 3), /* not LLC SNAP: 16 */
    /* 13 */ BPF_STMT(BPF_LD | BPF_W | BPF_ABS, 18),            /* organisation code bytes 2-3, protocol */
    /* 14 */ BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0x000c2000, 0, 1), /* CDP: 15, else 16 */
    /* 15 */ BPF_STMT(BPF_RET | BPF_K, CAPTURE_SNAP_LENGTH),    /* accept */
    /* 16 */ BPF_STMT(BPF_RET | BPF_K, 0),                      /* drop */
};

/* The two link-layer groups topology listens to: LLDP's nearest-bridge
 * group and CDP's group. */
static const unsigned char kTopologyGroups[2][ETH_ALEN] = {
    {0x01, 0x80, 0xc2, 0x00, 0x00, 0x0e},
    {0x01, 0x00, 0x0c, 0xcc, 0xcc, 0xcc},
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
            "usage: %s discover|watch|topology INTERFACE SECONDS\n"
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

/* Opens the packet socket on `name`, with `filter` locked on it, bound and
 * receiving; with `join_groups`, also listening to kTopologyGroups. Exits
 * with EXIT_USAGE, EXIT_NO_PERMISSION or EXIT_SOCKET_FAILED when it
 * cannot. */
static int open_socket(const char* name, const struct sock_fprog* filter, int join_groups) {
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

    const int lock = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_ATTACH_FILTER, filter, sizeof(*filter)) != 0 ||
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
    /* Only this socket's membership: the kernel drops it when the socket
     * closes. Nothing is sent to join a link-layer group. */
    for (size_t group = 0; join_groups && group < sizeof(kTopologyGroups) / sizeof(kTopologyGroups[0]); ++group) {
        struct packet_mreq membership;
        memset(&membership, 0, sizeof(membership));
        membership.mr_ifindex = (int)index;
        membership.mr_type = PACKET_MR_MULTICAST;
        membership.mr_alen = ETH_ALEN;
        memcpy(membership.mr_address, kTopologyGroups[group], ETH_ALEN);
        if (setsockopt(fd, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &membership, sizeof(membership)) != 0) {
            fail("could not listen for switches on the interface");
            _exit(EXIT_SOCKET_FAILED);
        }
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
    if (argc != 4 || (strcmp(argv[1], "discover") != 0 && strcmp(argv[1], "watch") != 0 &&
                      strcmp(argv[1], "topology") != 0)) {
        usage();
        return EXIT_USAGE;
    }
    /* The command picks one of the three fixed filters (and, for topology,
     * the two fixed groups), nothing else. */
    struct sock_fprog filter = {
        .len = (unsigned short)(sizeof(kFrameFilter) / sizeof(kFrameFilter[0])),
        .filter = kFrameFilter,
    };
    int join_groups = 0;
    if (strcmp(argv[1], "watch") == 0) {
        filter.len = (unsigned short)(sizeof(kWatchFilter) / sizeof(kWatchFilter[0]));
        filter.filter = kWatchFilter;
    } else if (strcmp(argv[1], "topology") == 0) {
        filter.len = (unsigned short)(sizeof(kTopologyFilter) / sizeof(kTopologyFilter[0]));
        filter.filter = kTopologyFilter;
        join_groups = 1;
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

    const int fd = open_socket(name, &filter, join_groups);
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
