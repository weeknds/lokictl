/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * lokictl-helper: the only part of LokiCTL that may change the firewall
 * or add a network interface (Share's hotspot, or the survey's listening
 * interface on a USB Wi-Fi stick).
 *
 * LokiCTL runs as the normal user. Blocking a device on its hotspot, and
 * adding the interface that Share runs its hotspot on, need one Linux
 * capability, CAP_NET_ADMIN, which this small program holds as a file
 * capability (set by the package's postinst; it is not setuid). Only root
 * and members of the netdev group may run it (root:netdev, mode 0750).
 *
 * It does exactly one of twelve fixed commands:
 *
 *   lokictl-helper block HOTSPOT MAC [MAC...]    block these devices on the hotspot
 *   lokictl-helper unblock HOTSPOT MAC [MAC...]  let them through again
 *   lokictl-helper list                  print the kill switch, the trap and the blocked devices
 *   lokictl-helper clear                 remove LokiCTL's firewall table
 *   lokictl-helper ap-add                add Share's interface (lokiap0)
 *   lokictl-helper ap-del                remove Share's interface
 *   lokictl-helper kill-on               arm the VPN kill switch
 *   lokictl-helper kill-off              disarm it
 *   lokictl-helper trap-on               send the trap's low ports on to LokiCTL, while it runs
 *   lokictl-helper trap-off              remove what a trap-on left behind
 *   lokictl-helper survey STICK          listen with a USB Wi-Fi stick (see below)
 *   lokictl-helper survey-off            remove the survey's listening interface
 *
 * HOTSPOT is the fixed word "router" (the hotspot on HELPER_INTERFACE,
 * wlan0) or "share" (on HELPER_SHARE_INTERFACE, lokiap0); no other text
 * is taken. A MAC is six pairs of hex digits joined by ':' (for example
 * 02:00:00:00:00:01), not a group address; at most HELPER_MAX_ADDRESSES per
 * call. Every address is parsed into six bytes and printed again, so no
 * text of the caller ever reaches the firewall.
 *
 * Every change goes into one nftables table of its own, "inet lokictl",
 * through /usr/sbin/nft with a fixed script: a set of blocked hardware
 * addresses and two base chains (input and forward, policy accept) that
 * drop what such a device sends in through the hotspot's interface. No
 * other table is ever touched, and the rule set is never flushed. Each
 * command runs as one nft transaction, so it is done whole or not at all,
 * and running it twice gives the same result. block and unblock make the
 * two rules match only the interface HOTSPOT names, and only while that
 * interface really runs a hotspot: "iw dev IF info" must say "type AP"
 * (exit 9 otherwise), so the uplink (wlan0 in Client mode) can never be
 * blocked.
 *
 * The kill switch (VPN gateway: hotspot devices may only go out through
 * LokiCTL's WireGuard tunnel, lokiwg0) has two base chains of its own in
 * the same table, so block and unblock, which empty the input and forward
 * chains, never touch it:
 * - kill_forward (forward hook) drops what comes in through either hotspot
 *   interface and would leave by any interface but lokiwg0. It matches the
 *   interface by name, so it holds while lokiwg0 is gone and after it is
 *   made again.
 * - kill_output (output hook) drops DNS (port 53, UDP and TCP) that the
 *   hotspot's DNS server sends by any interface but lokiwg0 or lo, so the
 *   names hotspot devices ask for never leave outside the tunnel either.
 *   Nothing else the Cardputer itself sends is touched. The DNS server is
 *   the dnsmasq that NetworkManager starts for a shared connection: kill-on
 *   reads the user of each dnsmasq whose parent is NetworkManager running
 *   as root (from /proc/PID/status) and matches that user. When none runs
 *   yet (the kill switch is armed before the hotspot starts) it takes
 *   dnsmasq's built-in default, HELPER_DNS_USER; LokiCTL arms it again once
 *   the hotspot is up. When one runs as root, or two run as different
 *   users, it changes nothing (exit 10).
 * "list" prints "kill-switch" on a line of its own while kill_forward
 * exists, "trap" while the trap chain exists, then the blocked devices.
 *
 * The trap (LokiCTL's honeypot) pretends to offer a few services. LokiCTL
 * itself listens, as the normal user, on fixed high ports; only the low
 * ones need this helper. trap-on makes a nat chain of its own in the same
 * table, "trap" (prerouting hook), that sends new IPv4 TCP connections
 * coming in through a fixed set of interfaces (TRAP_INTERFACES) to a fixed
 * low port of this machine itself on to its fixed high port (kTrapPorts:
 * 21, 23, 80 and 445 to 30021, 30023, 30080 and 30445). Only connections
 * addressed to this machine count ("fib daddr type local"): what it passes
 * on for a hotspot's devices is never touched. It takes no arguments.
 * A low port is sent on only while
 * - nothing on this machine listens on it (any address, IPv4 or IPv6, any
 *   user; read from /proc/net/tcp and /proc/net/tcp6), so a real service
 *   such as SSH on 22 is never taken over (22 and every other port are
 *   not in the map anyway), and
 * - a socket of the user who runs the helper listens on its high port, so
 *   the connections only ever reach LokiCTL.
 * trap-on keeps running while the trap does. It prints "ports" and the low
 * ports it sends on, on one line, and checks both conditions again every
 * TRAP_RECHECK_MS: a port that fails one is dropped for good (the chain is
 * set up again without it, and a new "ports" line is printed). Any input
 * on its standard input sets the chain up again at once (LokiCTL sends a
 * line after it removed the whole table). It ends when no port is left,
 * when its standard input closes or reaches its end (LokiCTL holds it open
 * while the trap runs, so it also ends when LokiCTL dies), on SIGTERM,
 * SIGINT or SIGHUP, or when its parent dies (PR_SET_PDEATHSIG), and then
 * removes the chain. trap-off removes a chain such a run left behind (it
 * was killed). Neither ever removes the table or anything else in it: the
 * chain goes in one transaction of its own ("add table", "add chain",
 * "flush chain", "delete chain"), so a kill switch armed meanwhile always
 * stays. An empty table may stay behind, which LokiCTL's "clear" removes.
 *
 * Share's interface goes through /usr/sbin/iw with fixed arguments only:
 * "iw dev wlan0 interface add lokiap0 type __ap" and "iw dev lokiap0 del".
 * Each is done at once when the interface is already there (or already
 * gone), so running it twice gives the same result. No other interface is
 * ever added or removed.
 *
 * The Wi-Fi survey uses an optional USB Wi-Fi stick, never the built-in
 * Wi-Fi. STICK is checked here before anything is done: 1 to 15 characters
 * of a-z, A-Z, 0-9, '.', '_' and '-' (not "." or ".."), never "wlan0" or
 * SURVEY_INTERFACE, an interface with a "wireless" folder in
 * /sys/class/net whose device is a USB interface ("DEVTYPE=usb_interface"
 * in its uevent). Only then is its name used, in a path under
 * /sys/class/net. The stick's own interface is never changed: the survey
 * listens through a second interface of its own, SURVEY_INTERFACE
 * (lokimon0), in monitor mode on the same stick. These commands touch no
 * firewall.
 * - survey: only a stick that is idle (down, in the normal managed mode,
 *   so nothing else uses it) is taken. Its interface index and its radio
 *   (the "phy" with its index, from /sys/class/net/STICK/phy80211) are
 *   noted, and the stick's name must still have the same index after
 *   every check. A SURVEY_INTERFACE a killed survey left behind is removed
 *   first. Then "iw phy PHY interface add lokimon0 type monitor" adds the
 *   listening interface on that radio, and it must turn out to be on the
 *   noted radio (else it is removed again), it is brought up, "ready" is
 *   printed on its own line, and it hops over the 2.4 GHz channels 1 to
 *   14, SURVEY_DWELL_MS on each ("iw dev lokimon0 set channel N"). A
 *   channel the kernel refuses (one the country's rules do not allow) is
 *   dropped for good, so it hops only the allowed ones. Receiving is all
 *   monitor mode does here: nothing is ever sent. It ends when its
 *   standard input closes or reaches its end (LokiCTL holds it open while
 *   it wants the survey, so it also ends when LokiCTL dies, however), on
 *   SIGTERM, SIGINT or SIGHUP, when its parent dies (PR_SET_PDEATHSIG),
 *   after SURVEY_MAX_SECONDS, or when the interface stops taking channels
 *   (the stick was pulled out). It then removes SURVEY_INTERFACE ("iw dev
 *   lokimon0 del"); the stick stays down and in managed mode throughout.
 * - survey-off: takes no argument and removes SURVEY_INTERFACE when it is
 *   there, as survey does at its end. LokiCTL uses it only after a survey
 *   that was killed.
 * Only a SURVEY_INTERFACE that is LokiCTL's kind (in monitor mode, on a
 * USB Wi-Fi stick) is ever removed; one that is gone already (the stick
 * was pulled out) counts as removed.
 *
 * Every nft and iw it runs gets an empty environment, no blocked signals,
 * and SIGKILL when this program dies (PR_SET_PDEATHSIG). One that has not
 * ended within HELPER_CHILD_LIMIT_MS is killed and counts as refused; so
 * is one that a stop of survey or trap-on interrupts (never the run that
 * removes what they made).
 *
 * Exit status: 0 done; 2 not a command, or a bad word, address or stick;
 * 3 the capability is missing; 4 nft refused the change; 5 nft could not
 * be run; 6 iw refused the change (or the stick could not be brought up or
 * down, or the listening interface was not on the stick's radio); 7 iw
 * could not be run; 8 the stick is in use (up, or not in managed mode), or
 * an interface of another kind is called lokimon0; 9 the hotspot named is
 * not running (its interface is not in access-point mode); 10 the
 * hotspot's DNS server runs as root, or as more than one user.
 */

#define _GNU_SOURCE

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/capability.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

enum {
    EXIT_DONE = 0,
    EXIT_USAGE = 2,
    EXIT_NO_PERMISSION = 3,
    EXIT_NFT_FAILED = 4,
    EXIT_NFT_NOT_RUN = 5,
    EXIT_IW_FAILED = 6,
    EXIT_IW_NOT_RUN = 7,
    EXIT_STICK_BUSY = 8,
    EXIT_NOT_HOTSPOT = 9,
    EXIT_DNS_USER = 10,
};

/* The most addresses one call takes. */
#define HELPER_MAX_ADDRESSES 16
/* The most elements the set holds. */
#define HELPER_SET_SIZE "64"
/* The built-in Wi-Fi: Router's hotspot runs on it, and Share's interface
 * is added on it. */
#define HELPER_INTERFACE "wlan0"
/* Share's interface, which runs its hotspot. */
#define HELPER_SHARE_INTERFACE "lokiap0"
/* The kill switch matches what comes in through either hotspot interface. */
#define HELPER_HOTSPOTS "{ \"" HELPER_INTERFACE "\", \"" HELPER_SHARE_INTERFACE "\" }"
/* LokiCTL's WireGuard tunnel: the only way out while the kill switch is
 * armed. */
#define HELPER_TUNNEL "lokiwg0"
/* The user the hotspot's DNS server runs as when none runs yet:
 * NetworkManager starts dnsmasq for a shared connection without --user, so
 * it drops to dnsmasq's built-in default, which Debian's dnsmasq-base
 * leaves at "nobody". kill-on reads the real one while dnsmasq runs. */
#define HELPER_DNS_USER "nobody"
/* The hotspot's DNS server and the program that starts it, as their
 * "Name:" line in /proc/PID/status shows them. */
#define HELPER_DNS_NAME "dnsmasq"
#define HELPER_DNS_PARENT "NetworkManager"
/* The interfaces the trap listens through: the cable, the built-in Wi-Fi
 * (also Router's hotspot) and Share's hotspot. */
#define TRAP_INTERFACES "{ \"eth0\", \"" HELPER_INTERFACE "\", \"" HELPER_SHARE_INTERFACE "\" }"
#define HELPER_NFT "/usr/sbin/nft"
#define HELPER_IW "/usr/sbin/iw"
/* The survey's listening interface, added on the stick in monitor mode.
 * The capture helper listens only on this name. */
#define SURVEY_INTERFACE "lokimon0"
/* The survey: how long it stays on each channel, the longest it listens,
 * and how many channel changes in a row may fail before the stick counts
 * as gone. */
#define SURVEY_DWELL_MS 250
#define SURVEY_MAX_SECONDS 900
#define SURVEY_MAX_FAILURES 3
/* The 2.4 GHz channels the survey tries. */
#define SURVEY_CHANNELS 14
/* How often a running trap-on checks again who listens. */
#define TRAP_RECHECK_MS 3000
/* The longest an nft or iw run may take before it is killed. */
#define HELPER_CHILD_LIMIT_MS 5000
/* "aa:bb:cc:dd:ee:ff" */
#define MAC_TEXT_LENGTH 17
/* The longest script: the fixed lines plus HELPER_MAX_ADDRESSES addresses. */
#define SCRIPT_MAX 2048
/* The most output of "list" that is read. */
#define OUTPUT_MAX 65536

static const char kProgram[] = "lokictl-helper";

/* The trap's fixed port map: low port, the high port LokiCTL listens on.
 * The same map is in src/model/trap_data.cpp. */
static const struct {
    unsigned int low;
    unsigned int high;
} kTrapPorts[] = {{21, 30021}, {23, 30023}, {80, 30080}, {445, 30445}};
#define TRAP_PORT_COUNT (sizeof(kTrapPorts) / sizeof(kTrapPorts[0]))
/* The most a /proc/net/tcp line is read of. */
#define PROC_LINE_MAX 512

/* The table, set and chains, made when missing. "add" leaves what is
 * there, and the two chains are emptied and get their one rule again (for
 * the hotspot's interface, filled in by ensure_table()), so the table
 * always ends up in exactly this shape. */
static const char kEnsureTable[] =
    "add table inet lokictl\n"
    "add set inet lokictl blocked { type ether_addr; size " HELPER_SET_SIZE "; }\n"
    "add chain inet lokictl input { type filter hook input priority filter - 10; policy accept; }\n"
    "add chain inet lokictl forward { type filter hook forward priority filter - 10; policy accept; }\n"
    "flush chain inet lokictl input\n"
    "flush chain inet lokictl forward\n";
#define ENSURE_RULES                                                                                  \
    "add rule inet lokictl input iifname \"%s\" ether saddr @blocked drop\n"                               \
    "add rule inet lokictl forward iifname \"%s\" ether saddr @blocked drop\n"

/* Adding the table first makes the delete work when there is none. */
static const char kClearTable[] =
    "add table inet lokictl\n"
    "delete table inet lokictl\n";

/* The kill switch's two chains, made when missing and emptied, so the
 * scripts below always start from the same state. A chain must be empty
 * before it can be deleted. */
static const char kKillChains[] =
    "add table inet lokictl\n"
    "add chain inet lokictl kill_forward { type filter hook forward priority filter - 5; policy accept; }\n"
    "add chain inet lokictl kill_output { type filter hook output priority filter - 5; policy accept; }\n"
    "flush chain inet lokictl kill_forward\n"
    "flush chain inet lokictl kill_output\n";

/* The kill switch's rules; %s is the DNS server's user (a name or a
 * number), filled in by kill_on(). */
#define KILL_ON_RULES                                                                                 \
    "add rule inet lokictl kill_forward iifname " HELPER_HOTSPOTS " oifname != \"" HELPER_TUNNEL "\" drop\n" \
    "add rule inet lokictl kill_output meta skuid %s oifname != { \"" HELPER_TUNNEL                      \
    "\", \"lo\" } udp dport 53 drop\n"                                                                  \
    "add rule inet lokictl kill_output meta skuid %s oifname != { \"" HELPER_TUNNEL                      \
    "\", \"lo\" } tcp dport 53 drop\n"

static const char kKillOff[] =
    "delete chain inet lokictl kill_forward\n"
    "delete chain inet lokictl kill_output\n";

/* The trap's chain, made when missing and emptied; trap-on adds one rule
 * per low port after it. */
#define TRAP_CHAIN                                                                                    \
    "add table inet lokictl\n"                                                                        \
    "add chain inet lokictl trap { type nat hook prerouting priority dstnat; policy accept; }\n"      \
    "flush chain inet lokictl trap\n"
static const char kTrapChain[] = TRAP_CHAIN;

/* Removes the trap's chain, also when there is none, and nothing else: the
 * table and every other chain stay. */
static const char kTrapRemove[] = TRAP_CHAIN "delete chain inet lokictl trap\n";

static void fail(const char* message) {
    fprintf(stderr, "%s: %s\n", kProgram, message);
}

static void usage(void) {
    fprintf(stderr,
            "usage: %s block HOTSPOT MAC [MAC...] | unblock HOTSPOT MAC [MAC...] | list | clear |\n"
            "       ap-add | ap-del | kill-on | kill-off | trap-on | trap-off | survey STICK | survey-off\n"
            "  HOTSPOT: router (wlan0) or share (lokiap0)\n"
            "  MAC: six pairs of hex digits joined by ':', at most %d per call\n"
            "  STICK: a USB Wi-Fi stick's interface, for example wlan1 (never wlan0)\n",
            kProgram,
            HELPER_MAX_ADDRESSES);
}

/* Closes every descriptor above 2 and makes sure 0, 1 and 2 are open, so
 * nothing the caller left open reaches nft, and no message lands in a file
 * that happens to sit on 1 or 2. */
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
                _exit(EXIT_NFT_NOT_RUN);
            }
        }
    }
}

static int hex_value(char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

/* Reads "aa:bb:cc:dd:ee:ff" (either case) into six bytes. Returns 0, or -1
 * for anything else, and for a group address (never a sender). */
static int parse_mac(const char* text, unsigned char bytes[6]) {
    if (strlen(text) != MAC_TEXT_LENGTH) {
        return -1;
    }
    for (int index = 0; index < 6; ++index) {
        const int high = hex_value(text[index * 3]);
        const int low = hex_value(text[index * 3 + 1]);
        if (high < 0 || low < 0 || (index < 5 && text[index * 3 + 2] != ':')) {
            return -1;
        }
        bytes[index] = (unsigned char)(high * 16 + low);
    }
    return (bytes[0] & 1U) ? -1 : 0;
}

/* Makes sure CAP_NET_ADMIN is ours and passes it on to nft: a file
 * capability does not survive execve(), an ambient one does. Returns 0, or
 * -1 when the capability is missing. */
static int pass_capability_on(void) {
    struct __user_cap_header_struct header;
    struct __user_cap_data_struct data[_LINUX_CAPABILITY_U32S_3];
    memset(&header, 0, sizeof(header));
    memset(data, 0, sizeof(data));
    header.version = _LINUX_CAPABILITY_VERSION_3;
    header.pid = 0;
    if (syscall(SYS_capget, &header, data) != 0) {
        return -1;
    }
    const unsigned int index = CAP_NET_ADMIN / 32;
    const __u32 bit = (__u32)1 << (CAP_NET_ADMIN % 32);
    if ((data[index].permitted & bit) == 0) {
        return -1;
    }
    data[index].inheritable |= bit;
    if (syscall(SYS_capset, &header, data) != 0) {
        return -1;
    }
    if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, CAP_NET_ADMIN, 0, 0) != 0) {
        return -1;
    }
    return 0;
}

/* Set by SIGTERM, SIGINT and SIGHUP while survey or trap-on runs. */
static volatile sig_atomic_t stop_requested = 0;

static void stop_signal(int number) {
    (void)number;
    stop_requested = 1;
}

static long elapsed_ms(const struct timespec* start) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - start->tv_sec) * 1000L + (now.tv_nsec - start->tv_nsec) / 1000000L;
}

/* Reads what waits in `fd` (non-blocking) into `output` (when not NULL,
 * at most output_size - 1 bytes in all, *used so far); the rest is dropped.
 * Returns 0 once the pipe reached its end (or failed), 1 while it is open. */
static int read_waiting(int fd, char* output, size_t output_size, size_t* used) {
    for (;;) {
        char chunk[1024];
        const ssize_t got = read(fd, chunk, sizeof(chunk));
        if (got < 0 && errno == EINTR) {
            continue;
        }
        if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return 1;
        }
        if (got <= 0) {
            return 0;
        }
        if (output) {
            const size_t room = output_size - 1 - *used;
            const size_t take = (size_t)got < room ? (size_t)got : room;
            memcpy(output + *used, chunk, take);
            *used += take;
        }
    }
}

/* Runs the program at `path` (nft or iw) with `arguments` (NULL-terminated,
 * fixed by this program), an empty environment, `input` on its standard
 * input and, when `output` is not NULL, its standard output read into
 * `output` (at most output_size - 1 bytes, ended by '\0'); otherwise its
 * standard output is read and dropped. Its messages go to this program's
 * standard error. It starts with no blocked signals, SIGPIPE at its
 * default, and SIGKILL as its parent-death signal, so it never outlives
 * this program. It is killed when it has not ended within
 * HELPER_CHILD_LIMIT_MS, and, with `may_stop`, as soon as a stop signal
 * came. Returns EXIT_DONE, EXIT_NFT_FAILED (it failed, or was killed) or
 * EXIT_NFT_NOT_RUN (it could not be run). */
static int run_program(const char* path, char* const arguments[], const char* input, char* output,
                       size_t output_size, int may_stop) {
    char* const environment[] = {"LC_ALL=C", NULL};
    int in_pipe[2] = {-1, -1};
    int out_pipe[2] = {-1, -1};
    int report[2] = {-1, -1};
    if (pipe2(in_pipe, O_CLOEXEC) != 0) {
        return EXIT_NFT_NOT_RUN;
    }
    if (pipe2(out_pipe, O_CLOEXEC) != 0) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        return EXIT_NFT_NOT_RUN;
    }
    /* Carries execve()'s error back; closes on a successful exec. */
    if (pipe2(report, O_CLOEXEC) != 0) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        return EXIT_NFT_NOT_RUN;
    }
    const pid_t self = getpid();
    const pid_t child = fork();
    if (child == 0) {
        /* Only async-signal-safe calls from here to execve(). */
        if (prctl(PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0) != 0 || getppid() != self ||
            dup2(in_pipe[0], 0) != 0 || dup2(out_pipe[1], 1) != 1) {
            _exit(127);
        }
        signal(SIGPIPE, SIG_DFL);
        sigset_t no_signals;
        sigemptyset(&no_signals);
        sigprocmask(SIG_SETMASK, &no_signals, NULL);
        execve(path, arguments, environment);
        const int error = errno;
        ssize_t ignored = write(report[1], &error, sizeof(error));
        (void)ignored;
        _exit(127);
    }
    close(in_pipe[0]);
    close(out_pipe[1]);
    close(report[1]);
    if (child < 0) {
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(report[0]);
        return EXIT_NFT_NOT_RUN;
    }
    int exec_error = 0;
    ssize_t reported = -1;
    do {
        reported = read(report[0], &exec_error, sizeof(exec_error));
    } while (reported < 0 && errno == EINTR);
    close(report[0]);
    if (reported > 0) {
        close(in_pipe[1]);
        close(out_pipe[0]);
        while (waitpid(child, NULL, 0) < 0 && errno == EINTR) {
        }
        return EXIT_NFT_NOT_RUN;
    }

    /* The script is far smaller than a pipe holds, so this never waits on
     * the program reading it. */
    size_t left = strlen(input);
    const char* next = input;
    while (left > 0) {
        const ssize_t written = write(in_pipe[1], next, left);
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            break;
        }
        next += written;
        left -= (size_t)written;
    }
    close(in_pipe[1]);

    /* Its output is read as it comes (its end tells that it is ending),
     * and the wait never passes the time limit. */
    fcntl(out_pipe[0], F_SETFL, fcntl(out_pipe[0], F_GETFL) | O_NONBLOCK);
    int out_open = 1;
    size_t used = 0;
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    int status = 0;
    int result = EXIT_DONE;
    for (;;) {
        if (out_open) {
            out_open = read_waiting(out_pipe[0], output, output_size, &used);
        }
        const pid_t done = waitpid(child, &status, WNOHANG);
        if (done == child) {
            /* What it wrote just before it ended. */
            if (out_open) {
                read_waiting(out_pipe[0], output, output_size, &used);
            }
            break;
        }
        if (done < 0 && errno != EINTR) {
            result = EXIT_NFT_NOT_RUN;
            break;
        }
        const long spent = elapsed_ms(&start);
        if (spent >= HELPER_CHILD_LIMIT_MS || (may_stop && stop_requested)) {
            if (spent >= HELPER_CHILD_LIMIT_MS) {
                fprintf(stderr, "%s: %s did not end in time and was stopped\n", kProgram, path);
            }
            kill(child, SIGKILL);
            while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
            }
            result = EXIT_NFT_FAILED;
            break;
        }
        /* Wakes when output comes or its end; otherwise looks again soon. */
        const long room = HELPER_CHILD_LIMIT_MS - spent;
        struct pollfd entry = {out_open ? out_pipe[0] : -1, POLLIN, 0};
        poll(&entry, 1, out_open ? (int)(room < 1000 ? room : 1000) : (int)(room < 5 ? room : 5));
    }
    close(out_pipe[0]);
    if (output) {
        output[used] = '\0';
    }
    if (result != EXIT_DONE) {
        return result;
    }
    return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? EXIT_DONE : EXIT_NFT_FAILED;
}

/* Appends `text` to the script. Returns -1 when it would not fit. */
static int append(char* script, size_t* used, const char* text) {
    const size_t length = strlen(text);
    if (*used + length >= SCRIPT_MAX) {
        return -1;
    }
    memcpy(script + *used, text, length + 1);
    *used += length;
    return 0;
}

/* Appends "<verb> element inet lokictl blocked { a, b }\n". */
static int append_elements(char* script,
                           size_t* used,
                           const char* verb,
                           unsigned char addresses[][6],
                           int count) {
    if (append(script, used, verb) != 0 || append(script, used, " element inet lokictl blocked { ") != 0) {
        return -1;
    }
    for (int index = 0; index < count; ++index) {
        char text[MAC_TEXT_LENGTH + 3];
        const unsigned char* bytes = addresses[index];
        snprintf(text,
                 sizeof(text),
                 "%s%02x:%02x:%02x:%02x:%02x:%02x",
                 index > 0 ? ", " : "",
                 bytes[0],
                 bytes[1],
                 bytes[2],
                 bytes[3],
                 bytes[4],
                 bytes[5]);
        if (append(script, used, text) != 0) {
            return -1;
        }
    }
    return append(script, used, " }\n");
}

/* Prints every hardware address in `text`, one per line, in lowercase. */
static void print_addresses(const char* text) {
    const size_t length = strlen(text);
    for (size_t start = 0; start + MAC_TEXT_LENGTH <= length; ++start) {
        char candidate[MAC_TEXT_LENGTH + 1];
        memcpy(candidate, text + start, MAC_TEXT_LENGTH);
        candidate[MAC_TEXT_LENGTH] = '\0';
        unsigned char bytes[6];
        /* A whole address only: not part of a longer run of hex digits. */
        const int clean_before = start == 0 || hex_value(text[start - 1]) < 0;
        const int clean_after = start + MAC_TEXT_LENGTH == length || hex_value(text[start + MAC_TEXT_LENGTH]) < 0;
        if (clean_before && clean_after && parse_mac(candidate, bytes) == 0) {
            printf("%02x:%02x:%02x:%02x:%02x:%02x\n", bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5]);
            start += MAC_TEXT_LENGTH - 1;
        }
    }
}

/* True when `text` has a line that reads `wanted` once the blanks in front
 * are skipped. */
static int has_line(const char* text, const char* wanted) {
    const size_t wanted_length = strlen(wanted);
    for (const char* line = text; *line != '\0';) {
        const char* end = strchr(line, '\n');
        const size_t length = end ? (size_t)(end - line) : strlen(line);
        size_t skip = 0;
        while (skip < length && (line[skip] == ' ' || line[skip] == '\t')) {
            ++skip;
        }
        if (length - skip == wanted_length && strncmp(line + skip, wanted, wanted_length) == 0) {
            return 1;
        }
        line += length + (end ? 1 : 0);
    }
    return 0;
}

static int list_blocked(void) {
    static char output[OUTPUT_MAX];
    char* const tables[] = {"nft", "list", "tables", "inet", NULL};
    int result = run_program(HELPER_NFT, tables, "", output, sizeof(output), 0);
    if (result != EXIT_DONE) {
        return result;
    }
    /* Without the table nothing is blocked and the kill switch is off. */
    if (!has_line(output, "table inet lokictl")) {
        return EXIT_DONE;
    }
    char* const table[] = {"nft", "list", "table", "inet", "lokictl", NULL};
    result = run_program(HELPER_NFT, table, "", output, sizeof(output), 0);
    if (result == EXIT_DONE) {
        if (has_line(output, "chain kill_forward {")) {
            printf("kill-switch\n");
        }
        if (has_line(output, "chain trap {")) {
            printf("trap\n");
        }
        /* The rules hold no hardware address, so every one is in the set. */
        print_addresses(output);
    }
    return result;
}

/* Runs nft with `script` on its standard input and says what went wrong;
 * with `may_stop`, a stop signal ends the run (see run_program()). */
static int run_script(const char* script, int may_stop) {
    char* const from_input[] = {"nft", "-f", "-", NULL};
    const int result = run_program(HELPER_NFT, from_input, script, NULL, 0, may_stop);
    if (result == EXIT_NFT_NOT_RUN) {
        fail("could not run " HELPER_NFT);
    } else if (result == EXIT_NFT_FAILED) {
        fail("nft refused the change");
    }
    return result;
}

/* Reads the listening TCP sockets in `path` (/proc/net/tcp or
 * /proc/net/tcp6). For each pair of kTrapPorts it marks low_busy when
 * anything listens on the low port, and high_ours when a socket of `uid`
 * listens on the high port. Returns 0, or -1 when the file cannot be
 * opened (errno says why). */
static int read_listeners(const char* path, uid_t uid, int low_busy[], int high_ours[]) {
    FILE* file = fopen(path, "re");
    if (!file) {
        return -1;
    }
    char line[PROC_LINE_MAX];
    /* The first line holds the column names. */
    if (fgets(line, sizeof(line), file)) {
        while (fgets(line, sizeof(line), file)) {
            unsigned int port = 0;
            unsigned int state = 0;
            unsigned long owner = 0;
            /* "  0: 0100007F:0016 00000000:0000 0A 00000000:00000000 00:00000000 00000000  1000 ..." */
            if (sscanf(line,
                       " %*u: %*[0-9A-Fa-f]:%x %*[0-9A-Fa-f]:%*x %x %*x:%*x %*x:%*x %*x %lu",
                       &port,
                       &state,
                       &owner) != 3 ||
                state != 0x0A /* listening */) {
                continue;
            }
            for (size_t index = 0; index < TRAP_PORT_COUNT; ++index) {
                if (port == kTrapPorts[index].low) {
                    low_busy[index] = 1;
                }
                if (port == kTrapPorts[index].high && owner == (unsigned long)uid) {
                    high_ours[index] = 1;
                }
            }
        }
    }
    fclose(file);
    return 0;
}

/* Which pairs of kTrapPorts may be sent on now: nothing listens on the
 * low port and a socket of `uid` listens on the high one. Anything that
 * cannot be read counts as busy: then none may. Without IPv6 there is no
 * tcp6 file. */
static void trap_allowed(uid_t uid, int allowed[]) {
    int low_busy[TRAP_PORT_COUNT] = {0};
    int high_ours[TRAP_PORT_COUNT] = {0};
    int readable = read_listeners("/proc/net/tcp", uid, low_busy, high_ours) == 0;
    if (readable && read_listeners("/proc/net/tcp6", uid, low_busy, high_ours) != 0 && errno != ENOENT) {
        readable = 0;
    }
    for (size_t index = 0; index < TRAP_PORT_COUNT; ++index) {
        allowed[index] = readable && !low_busy[index] && high_ours[index];
    }
}

/* Sets the trap's chain up (in one transaction) with a rule for each pair
 * of kTrapPorts marked in `on`. */
static int trap_apply(const int on[]) {
    char script[SCRIPT_MAX];
    size_t used = 0;
    script[0] = '\0';
    int built = append(script, &used, kTrapChain);
    for (size_t index = 0; built == 0 && index < TRAP_PORT_COUNT; ++index) {
        if (!on[index]) {
            continue;
        }
        char rule[256];
        snprintf(rule,
                 sizeof(rule),
                 "add rule inet lokictl trap iifname " TRAP_INTERFACES
                 " meta nfproto ipv4 fib daddr type local tcp dport %u redirect to :%u\n",
                 kTrapPorts[index].low,
                 kTrapPorts[index].high);
        built = append(script, &used, rule);
    }
    if (built != 0) {
        fail("internal error: the script is too long");
        return EXIT_NFT_NOT_RUN;
    }
    return run_script(script, 1);
}

/* Prints "ports" and the low ports marked in `on` on one line. */
static void trap_print(const int on[]) {
    printf("ports");
    for (size_t index = 0; index < TRAP_PORT_COUNT; ++index) {
        if (on[index]) {
            printf(" %u", kTrapPorts[index].low);
        }
    }
    printf("\n");
    fflush(stdout);
}

/* Makes a long-running command end with its parent: the parent-death
 * signal, and the check after it, which catches a parent that died before
 * it was set; then SIGTERM, SIGINT and SIGHUP only ask it to stop. Returns
 * 0, or -1 when the parent is gone already. */
static int hold_with_parent(pid_t parent) {
    prctl(PR_SET_PDEATHSIG, SIGTERM, 0, 0, 0);
    if (getppid() != parent) {
        return -1;
    }
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = stop_signal;
    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGHUP, &action, NULL);
    return 0;
}

/* Waits `ms` milliseconds on standard input. Returns 1 when the run should
 * end: input closed or at its end, a stop signal, or an error. With
 * `poked`, input that arrives ends the wait early and sets *poked;
 * without, it is read and dropped. */
static int wait_input(int ms, int* poked) {
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (;;) {
        if (stop_requested) {
            return 1;
        }
        const long done = elapsed_ms(&start);
        if (done >= ms) {
            return 0;
        }
        struct pollfd entry = {0, POLLIN, 0};
        const int ready = poll(&entry, 1, (int)(ms - done));
        if (ready < 0 && errno == EINTR) {
            continue;
        }
        if (ready < 0 || (entry.revents & POLLNVAL)) {
            return 1;
        }
        if (entry.revents & (POLLIN | POLLHUP | POLLERR)) {
            char ignored[64];
            const ssize_t got = read(0, ignored, sizeof(ignored));
            if (got < 0 && (errno == EINTR || errno == EAGAIN)) {
                continue;
            }
            if (got <= 0) {
                return 1;
            }
            if (poked) {
                *poked = 1;
                return 0;
            }
        }
    }
}

/* trap-off: removes the trap's chain, and nothing else. Without the table
 * there is nothing to remove (and no empty table is made). */
static int trap_off(void) {
    static char output[OUTPUT_MAX];
    char* const tables[] = {"nft", "list", "tables", "inet", NULL};
    if (run_program(HELPER_NFT, tables, "", output, sizeof(output), 0) == EXIT_DONE &&
        !has_line(output, "table inet lokictl")) {
        return EXIT_DONE;
    }
    return run_script(kTrapRemove, 0);
}

/* trap-on: sends the low ports of kTrapPorts on while the trap runs (see
 * the top of this file), then removes its chain. */
static int trap_on(pid_t parent) {
    if (hold_with_parent(parent) != 0) {
        return EXIT_DONE;
    }
    const uid_t uid = getuid();
    int on[TRAP_PORT_COUNT];
    trap_allowed(uid, on);
    int count = 0;
    for (size_t index = 0; index < TRAP_PORT_COUNT; ++index) {
        count += on[index];
    }
    int result = count > 0 ? trap_apply(on) : EXIT_DONE;
    if (count > 0 && result == EXIT_DONE) {
        trap_print(on);
        for (;;) {
            int poked = 0;
            if (wait_input(TRAP_RECHECK_MS, &poked)) {
                break;
            }
            /* A port only ever goes: one that fails a condition is not
             * sent on again in this run. */
            int now[TRAP_PORT_COUNT];
            trap_allowed(uid, now);
            int changed = 0;
            count = 0;
            for (size_t index = 0; index < TRAP_PORT_COUNT; ++index) {
                now[index] = now[index] && on[index];
                changed = changed || now[index] != on[index];
                count += now[index];
            }
            if (count == 0) {
                memcpy(on, now, sizeof(on));
                break;
            }
            if (changed || poked) {
                result = trap_apply(now);
                if (result != EXIT_DONE) {
                    break;
                }
                if (changed) {
                    memcpy(on, now, sizeof(on));
                    trap_print(on);
                }
            }
        }
    }
    /* A run a stop cut short is no failure. */
    if (stop_requested) {
        result = EXIT_DONE;
    }
    if (result != EXIT_DONE || count == 0) {
        int none[TRAP_PORT_COUNT] = {0};
        trap_print(none);
    }
    const int removed = trap_off();
    return result != EXIT_DONE ? result : removed;
}

/* block and unblock: whether `name` (HELPER_INTERFACE or
 * HELPER_SHARE_INTERFACE) runs a hotspot right now: "iw dev NAME info"
 * says "type AP". Returns EXIT_DONE, EXIT_NOT_HOTSPOT or EXIT_IW_NOT_RUN. */
static int hotspot_running(const char* name) {
    static char output[OUTPUT_MAX];
    char interface_name[IFNAMSIZ];
    snprintf(interface_name, sizeof(interface_name), "%s", name);
    char* const arguments[] = {"iw", "dev", interface_name, "info", NULL};
    const int result = run_program(HELPER_IW, arguments, "", output, sizeof(output), 0);
    if (result == EXIT_NFT_NOT_RUN) {
        fail("could not run " HELPER_IW);
        return EXIT_IW_NOT_RUN;
    }
    if (result != EXIT_DONE || !has_line(output, "type AP")) {
        fprintf(stderr, "%s: no hotspot runs on %s (not in access-point mode)\n", kProgram, interface_name);
        return EXIT_NOT_HOTSPOT;
    }
    return EXIT_DONE;
}

/* Reads "Name:", "PPid:" and the four user ids of "Uid:" (real,
 * effective, saved, file system) from /proc/<pid>/status. Returns 0, or
 * -1 when the process is gone or the file is not as expected. */
static int read_process(const char* pid, char name[64], long* parent, unsigned long uids[4]) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%s/status", pid);
    FILE* file = fopen(path, "re");
    if (!file) {
        return -1;
    }
    char line[256];
    int found = 0;
    name[0] = '\0';
    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "Name:", 5) == 0 && sscanf(line + 5, " %63s", name) == 1) {
            found |= 1;
        } else if (strncmp(line, "PPid:", 5) == 0 && sscanf(line + 5, " %ld", parent) == 1) {
            found |= 2;
        } else if (strncmp(line, "Uid:", 4) == 0 &&
                   sscanf(line + 4, " %lu %lu %lu %lu", &uids[0], &uids[1], &uids[2], &uids[3]) == 4) {
            found |= 4;
        }
    }
    fclose(file);
    return found == 7 ? 0 : -1;
}

/* kill-on: the user the hotspot's DNS server runs as, into `user` (as nft
 * takes it): the file-system user id (the one its sockets carry) of every
 * HELPER_DNS_NAME process whose parent is HELPER_DNS_PARENT running as
 * root; a process of that name started by anyone else is not the
 * hotspot's. HELPER_DNS_USER when none runs. Returns 0, or -1 when one
 * runs as root or two run as different users. */
static int dns_server_user(char user[16]) {
    DIR* folder = opendir("/proc");
    if (!folder) {
        snprintf(user, 16, "%s", HELPER_DNS_USER);
        return 0;
    }
    int found = 0;
    int mixed = 0;
    unsigned long uid = 0;
    struct dirent* entry;
    while ((entry = readdir(folder)) != NULL) {
        if (entry->d_name[0] < '1' || entry->d_name[0] > '9' ||
            strspn(entry->d_name, "0123456789") != strlen(entry->d_name) || strlen(entry->d_name) > 10) {
            continue;
        }
        char name[64];
        long parent = 0;
        unsigned long uids[4];
        if (read_process(entry->d_name, name, &parent, uids) != 0 || strcmp(name, HELPER_DNS_NAME) != 0 ||
            parent <= 0) {
            continue;
        }
        char parent_pid[24];
        char parent_name[64];
        long grandparent = 0;
        unsigned long parent_uids[4];
        snprintf(parent_pid, sizeof(parent_pid), "%ld", parent);
        if (read_process(parent_pid, parent_name, &grandparent, parent_uids) != 0 ||
            strcmp(parent_name, HELPER_DNS_PARENT) != 0 || parent_uids[0] != 0 || parent_uids[1] != 0) {
            continue;
        }
        if (found && uids[3] != uid) {
            mixed = 1;
        }
        uid = uids[3];
        found = 1;
    }
    closedir(folder);
    if (mixed || (found && uid == 0)) {
        return -1;
    }
    if (found) {
        snprintf(user, 16, "%lu", uid);
    } else {
        snprintf(user, 16, "%s", HELPER_DNS_USER);
    }
    return 0;
}

/* kill-on: arms the kill switch for the DNS server's user. */
static int kill_on(void) {
    char user[16];
    if (dns_server_user(user) != 0) {
        fail("the hotspot's DNS server runs as root, or as more than one user; nothing changed");
        return EXIT_DNS_USER;
    }
    char script[SCRIPT_MAX];
    size_t used = 0;
    script[0] = '\0';
    char rules[1024];
    snprintf(rules, sizeof(rules), KILL_ON_RULES, user, user);
    if (append(script, &used, kKillChains) != 0 || append(script, &used, rules) != 0) {
        fail("internal error: the script is too long");
        return EXIT_NFT_NOT_RUN;
    }
    return run_script(script, 0);
}

/* Adds (add != 0) or removes Share's interface, unless it is already
 * there (or already gone). */
static int share_interface(int add) {
    const int present = access("/sys/class/net/" HELPER_SHARE_INTERFACE, F_OK) == 0;
    if (present == (add != 0)) {
        return EXIT_DONE;
    }
    char* const add_arguments[] = {
        "iw", "dev", HELPER_INTERFACE, "interface", "add", HELPER_SHARE_INTERFACE, "type", "__ap", NULL};
    char* const delete_arguments[] = {"iw", "dev", HELPER_SHARE_INTERFACE, "del", NULL};
    const int result = run_program(HELPER_IW, add ? add_arguments : delete_arguments, "", NULL, 0, 0);
    if (result == EXIT_NFT_NOT_RUN) {
        fail("could not run " HELPER_IW);
        return EXIT_IW_NOT_RUN;
    }
    if (result == EXIT_NFT_FAILED) {
        fail("iw refused the change");
        return EXIT_IW_FAILED;
    }
    return EXIT_DONE;
}


/* ---------------------------------------------------------------------
 * The Wi-Fi survey's stick. */

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

/* Whether the interface `name` (already checked) has a "wireless" folder
 * and sits on a USB interface. */
static int usb_wifi_interface(const char* name) {
    char path[96];
    struct stat info;
    snprintf(path, sizeof(path), "/sys/class/net/%s/wireless", name);
    if (stat(path, &info) != 0 || !S_ISDIR(info.st_mode)) {
        return 0;
    }
    snprintf(path, sizeof(path), "/sys/class/net/%s/device/uevent", name);
    FILE* file = fopen(path, "re");
    if (!file) {
        return 0;
    }
    char line[256];
    int usb = 0;
    while (fgets(line, sizeof(line), file)) {
        if (strcmp(line, "DEVTYPE=usb_interface\n") == 0 || strcmp(line, "DEVTYPE=usb_interface") == 0) {
            usb = 1;
        }
    }
    fclose(file);
    return usb;
}

/* Whether `name` is a USB Wi-Fi stick: a valid name that is not the
 * built-in Wi-Fi or the survey's own listening interface, with a
 * "wireless" folder, on a USB interface. The name is checked before it is
 * put into a path. */
static int valid_stick(const char* name) {
    if (!valid_interface_name(name) || strcmp(name, HELPER_INTERFACE) == 0 || strcmp(name, SURVEY_INTERFACE) == 0) {
        return 0;
    }
    return usb_wifi_interface(name);
}

/* The stick's interface flags into *flags. Returns 0, or -1. */
static int stick_flags(const char* name, short* flags) {
    const int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return -1;
    }
    struct ifreq request;
    memset(&request, 0, sizeof(request));
    memcpy(request.ifr_name, name, strlen(name));
    const int result = ioctl(fd, SIOCGIFFLAGS, &request);
    close(fd);
    if (result != 0) {
        return -1;
    }
    *flags = request.ifr_flags;
    return 0;
}

/* Brings the stick up (up != 0) or down. Returns 0, or -1. */
static int stick_set_up(const char* name, int up) {
    const int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return -1;
    }
    struct ifreq request;
    memset(&request, 0, sizeof(request));
    memcpy(request.ifr_name, name, strlen(name));
    int result = ioctl(fd, SIOCGIFFLAGS, &request);
    if (result == 0) {
        if (up) {
            request.ifr_flags |= IFF_UP;
        } else {
            request.ifr_flags &= ~IFF_UP;
        }
        result = ioctl(fd, SIOCSIFFLAGS, &request);
    }
    close(fd);
    return result == 0 ? 0 : -1;
}

/* The stick's link type, from the kernel (1 managed, 803 monitor). Returns
 * it, or -1. */
static int stick_type(const char* name) {
    char path[96];
    snprintf(path, sizeof(path), "/sys/class/net/%s/type", name);
    FILE* file = fopen(path, "re");
    if (!file) {
        return -1;
    }
    int type = -1;
    if (fscanf(file, "%d", &type) != 1) {
        type = -1;
    }
    fclose(file);
    return type;
}

/* The interface's radio, from /sys/class/net/<name>/phy80211: its name
 * (checked like an interface name) into `phy` and its index into *index.
 * Returns 0, or -1. */
static int stick_phy(const char* name, char phy[32], long* index) {
    char path[96];
    snprintf(path, sizeof(path), "/sys/class/net/%s/phy80211/name", name);
    FILE* file = fopen(path, "re");
    if (!file) {
        return -1;
    }
    char text[64] = "";
    const int read_name = fscanf(file, "%63s", text) == 1;
    fclose(file);
    if (!read_name || strlen(text) >= 32 || !valid_interface_name(text)) {
        return -1;
    }
    snprintf(path, sizeof(path), "/sys/class/net/%s/phy80211/index", name);
    file = fopen(path, "re");
    if (!file) {
        return -1;
    }
    long value = -1;
    const int read_index = fscanf(file, "%ld", &value) == 1;
    fclose(file);
    if (!read_index || value < 0) {
        return -1;
    }
    snprintf(phy, 32, "%s", text);
    *index = value;
    return 0;
}

/* Removes the survey's listening interface (SURVEY_INTERFACE) when it is
 * there, but only one of LokiCTL's kind: in monitor mode, on a USB Wi-Fi
 * stick. Returns EXIT_DONE (also when it is gone, or went while this ran:
 * the stick was pulled out), EXIT_STICK_BUSY (another kind of interface
 * has the name), EXIT_IW_FAILED or EXIT_IW_NOT_RUN. */
static int listener_remove(void) {
    if (access("/sys/class/net/" SURVEY_INTERFACE, F_OK) != 0) {
        return EXIT_DONE;
    }
    if (!usb_wifi_interface(SURVEY_INTERFACE) || stick_type(SURVEY_INTERFACE) != ARPHRD_IEEE80211_RADIOTAP) {
        if (access("/sys/class/net/" SURVEY_INTERFACE, F_OK) != 0) {
            return EXIT_DONE;
        }
        fail("another kind of interface is called " SURVEY_INTERFACE);
        return EXIT_STICK_BUSY;
    }
    char* const arguments[] = {"iw", "dev", SURVEY_INTERFACE, "del", NULL};
    const int result = run_program(HELPER_IW, arguments, "", NULL, 0, 0);
    if (result == EXIT_NFT_NOT_RUN) {
        fail("could not run " HELPER_IW);
        return EXIT_IW_NOT_RUN;
    }
    if (result != EXIT_DONE && access("/sys/class/net/" SURVEY_INTERFACE, F_OK) == 0) {
        fail("could not remove " SURVEY_INTERFACE);
        return EXIT_IW_FAILED;
    }
    return EXIT_DONE;
}

/* survey: add the listening interface, hop, remove it (see the top of
 * this file). */
static int survey(const char* name, pid_t parent) {
    if (hold_with_parent(parent) != 0) {
        return EXIT_DONE;
    }

    /* Everything below that goes by the stick's name is checked to have
     * reached the same interface: its index before and after. */
    const unsigned int stick_index = if_nametoindex(name);
    short flags = 0;
    if (stick_index == 0 || stick_flags(name, &flags) != 0) {
        fail("no such stick");
        return EXIT_USAGE;
    }
    if ((flags & IFF_UP) != 0 || stick_type(name) != ARPHRD_ETHER) {
        fail("the stick is in use (up, or not in managed mode)");
        return EXIT_STICK_BUSY;
    }
    char phy[32];
    long phy_index = -1;
    if (stick_phy(name, phy, &phy_index) != 0 || !valid_stick(name) || if_nametoindex(name) != stick_index) {
        fail("not a USB Wi-Fi stick (it changed while it was checked)");
        return EXIT_USAGE;
    }
    /* One a killed survey left behind goes first. */
    int result = listener_remove();
    if (result != EXIT_DONE) {
        return result;
    }
    char* const add_arguments[] = {
        "iw", "phy", phy, "interface", "add", SURVEY_INTERFACE, "type", "monitor", NULL};
    result = run_program(HELPER_IW, add_arguments, "", NULL, 0, 1);
    if (result == EXIT_NFT_NOT_RUN) {
        fail("could not run " HELPER_IW);
        return EXIT_IW_NOT_RUN;
    }
    /* The new interface must be on the noted radio, and the stick's name
     * still on the noted interface. */
    char added_phy[32];
    long added_index = -1;
    const int on_stick = result == EXIT_DONE && stick_phy(SURVEY_INTERFACE, added_phy, &added_index) == 0 &&
                         added_index == phy_index && if_nametoindex(name) == stick_index;
    if (!on_stick || stop_requested || stick_set_up(SURVEY_INTERFACE, 1) != 0) {
        if (!stop_requested) {
            fail(on_stick ? "the stick could not listen (monitor mode)"
                          : "the listening interface is not on the stick");
        }
        const int removed = listener_remove();
        return stop_requested && removed == EXIT_DONE ? EXIT_DONE : EXIT_IW_FAILED;
    }
    printf("ready\n");
    fflush(stdout);

    /* 0: not tried yet, 1: the kernel took it, -1: refused (dropped). */
    int channels[SURVEY_CHANNELS + 1] = {0};
    int failures = 0;
    int usable = SURVEY_CHANNELS;
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    int ended = 0;
    while (!ended && usable > 0) {
        for (int channel = 1; channel <= SURVEY_CHANNELS && !ended; ++channel) {
            if (channels[channel] < 0) {
                continue;
            }
            char number[4];
            snprintf(number, sizeof(number), "%d", channel);
            char* const arguments[] = {"iw", "dev", SURVEY_INTERFACE, "set", "channel", number, NULL};
            result = run_program(HELPER_IW, arguments, "", NULL, 0, 1);
            if (stop_requested) {
                ended = 1;
                break;
            }
            if (result == EXIT_DONE) {
                channels[channel] = 1;
                failures = 0;
            } else if (channels[channel] == 0) {
                channels[channel] = -1; /* not allowed here */
                --usable;
                continue;
            } else if (++failures >= SURVEY_MAX_FAILURES) {
                fail("the stick stopped taking channels");
                ended = 1;
                break;
            }
            ended = wait_input(SURVEY_DWELL_MS, NULL) || elapsed_ms(&start) >= SURVEY_MAX_SECONDS * 1000L;
        }
    }
    return listener_remove();
}

int main(int argc, char* argv[]) {
    /* First: no signal blocked and SIGCHLD at its default, whatever the
     * caller left (a blocked SIGTERM would keep a long run going; an
     * ignored SIGCHLD would lose every child's exit status). Then who
     * started it, for the parent checks of survey and trap-on. */
    sigset_t no_signals;
    sigemptyset(&no_signals);
    sigprocmask(SIG_SETMASK, &no_signals, NULL);
    signal(SIGCHLD, SIG_DFL);
    const pid_t parent = getppid();
    tidy_descriptors();
    signal(SIGPIPE, SIG_IGN);

    if (argc < 2) {
        usage();
        return EXIT_USAGE;
    }
    const char* command = argv[1];
    const int block = strcmp(command, "block") == 0;
    const int unblock = strcmp(command, "unblock") == 0;
    const int list = strcmp(command, "list") == 0;
    const int clear = strcmp(command, "clear") == 0;
    const int ap_add = strcmp(command, "ap-add") == 0;
    const int ap_del = strcmp(command, "ap-del") == 0;
    const int kill_start = strcmp(command, "kill-on") == 0;
    const int kill_stop = strcmp(command, "kill-off") == 0;
    const int trap_start = strcmp(command, "trap-on") == 0;
    const int trap_stop = strcmp(command, "trap-off") == 0;
    const int survey_start = strcmp(command, "survey") == 0;
    const int survey_end = strcmp(command, "survey-off") == 0;
    if (!block && !unblock && !list && !clear && !ap_add && !ap_del && !kill_start && !kill_stop && !trap_start &&
        !trap_stop && !survey_start && !survey_end) {
        usage();
        return EXIT_USAGE;
    }

    /* Every argument is checked before anything runs. */
    unsigned char addresses[HELPER_MAX_ADDRESSES][6];
    int count = 0;
    const char* hotspot = NULL;
    if (block || unblock) {
        if (argc < 4 || argc - 3 > HELPER_MAX_ADDRESSES) {
            usage();
            return EXIT_USAGE;
        }
        if (strcmp(argv[2], "router") == 0) {
            hotspot = HELPER_INTERFACE;
        } else if (strcmp(argv[2], "share") == 0) {
            hotspot = HELPER_SHARE_INTERFACE;
        } else {
            fail("not a hotspot (router or share)");
            return EXIT_USAGE;
        }
        for (int index = 3; index < argc; ++index) {
            unsigned char bytes[6];
            if (parse_mac(argv[index], bytes) != 0) {
                fail("not a device address (six pairs of hex digits joined by ':')");
                return EXIT_USAGE;
            }
            int seen = 0;
            for (int other = 0; other < count; ++other) {
                seen = seen || memcmp(addresses[other], bytes, 6) == 0;
            }
            if (!seen) {
                memcpy(addresses[count++], bytes, 6);
            }
        }
    } else if (survey_start) {
        if (argc != 3) {
            usage();
            return EXIT_USAGE;
        }
        if (!valid_stick(argv[2])) {
            fail("not a USB Wi-Fi stick (and never wlan0)");
            return EXIT_USAGE;
        }
    } else if (argc != 2) {
        usage();
        return EXIT_USAGE;
    }

    if (pass_capability_on() != 0) {
        fail("missing the network-admin capability (reinstall the lokictl package)");
        return EXIT_NO_PERMISSION;
    }

    if (list) {
        return list_blocked();
    }
    if (ap_add || ap_del) {
        return share_interface(ap_add);
    }
    if (trap_start) {
        return trap_on(parent);
    }
    if (trap_stop) {
        return trap_off();
    }
    if (survey_start) {
        return survey(argv[2], parent);
    }
    if (survey_end) {
        return listener_remove();
    }
    if (kill_start) {
        return kill_on();
    }

    char script[SCRIPT_MAX];
    size_t used = 0;
    script[0] = '\0';
    int built = 0;
    if (clear) {
        built = append(script, &used, kClearTable);
    } else if (kill_stop) {
        built = append(script, &used, kKillChains);
        if (built == 0) {
            built = append(script, &used, kKillOff);
        }
    } else {
        const int running = hotspot_running(hotspot);
        if (running != EXIT_DONE) {
            return running;
        }
        char rules[256];
        snprintf(rules, sizeof(rules), ENSURE_RULES, hotspot, hotspot);
        built = append(script, &used, kEnsureTable);
        if (built == 0) {
            built = append(script, &used, rules);
        }
        if (built == 0 && block) {
            built = append_elements(script, &used, "add", addresses, count);
        } else if (built == 0) {
            /* Adding first makes the delete work for a device that is not
             * blocked. */
            built = append_elements(script, &used, "add", addresses, count);
            if (built == 0) {
                built = append_elements(script, &used, "delete", addresses, count);
            }
        }
    }
    if (built != 0) {
        fail("internal error: the script is too long");
        return EXIT_NFT_NOT_RUN;
    }

    return run_script(script, 0);
}
