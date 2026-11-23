/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * lokictl-helper: the only part of LokiCTL that may change the firewall
 * or add a network interface.
 *
 * LokiCTL runs as the normal user. Blocking a device on its hotspot, and
 * adding the interface that Share runs its hotspot on, need one Linux
 * capability, CAP_NET_ADMIN, which this small program holds as a file
 * capability (set by the package's postinst; it is not setuid). Only root
 * and members of the netdev group may run it (root:netdev, mode 0750).
 *
 * It does exactly one of eight fixed commands and exits:
 *
 *   lokictl-helper block MAC [MAC...]    block these devices on the hotspot
 *   lokictl-helper unblock MAC [MAC...]  let them through again
 *   lokictl-helper list                  print the kill switch and the blocked devices
 *   lokictl-helper clear                 remove LokiCTL's firewall table
 *   lokictl-helper ap-add                add Share's interface (lokiap0)
 *   lokictl-helper ap-del                remove Share's interface
 *   lokictl-helper kill-on               arm the VPN kill switch
 *   lokictl-helper kill-off              disarm it
 *
 * A MAC is six pairs of hex digits joined by ':' (for example
 * 02:00:00:00:00:01), not a group address; at most HELPER_MAX_ADDRESSES per
 * call. Every address is parsed into six bytes and printed again, so no
 * text of the caller ever reaches the firewall.
 *
 * Every change goes into one nftables table of its own, "inet lokictl",
 * through /usr/sbin/nft with a fixed script: a set of blocked hardware
 * addresses and two base chains (input and forward, policy accept) that
 * drop what such a device sends in through either hotspot interface
 * (Router's wlan0, Share's lokiap0). No other table is ever touched, and
 * the rule set is never flushed. Each command runs as one nft transaction,
 * so it is done whole or not at all, and running it twice gives the same
 * result.
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
 *   hotspot's DNS server (dnsmasq, which runs as HELPER_DNS_USER) sends by
 *   any interface but lokiwg0 or lo, so the names hotspot devices ask for
 *   never leave outside the tunnel either. Nothing else the Cardputer
 *   itself sends is touched.
 * "list" prints "kill-switch" on a line of its own while kill_forward
 * exists, then the blocked devices.
 *
 * Share's interface goes through /usr/sbin/iw with fixed arguments only:
 * "iw dev wlan0 interface add lokiap0 type __ap" and "iw dev lokiap0 del".
 * Each is done at once when the interface is already there (or already
 * gone), so running it twice gives the same result. No other interface is
 * ever added or removed.
 *
 * Exit status: 0 done; 2 not a command, or a bad address; 3 the capability
 * is missing; 4 nft refused the change; 5 nft could not be run; 6 iw
 * refused the change; 7 iw could not be run.
 */

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <linux/capability.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
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
/* The rules match only what comes in through the two hotspot interfaces. */
#define HELPER_HOTSPOTS "{ \"" HELPER_INTERFACE "\", \"" HELPER_SHARE_INTERFACE "\" }"
/* LokiCTL's WireGuard tunnel: the only way out while the kill switch is
 * armed. */
#define HELPER_TUNNEL "lokiwg0"
/* The user the hotspot's DNS server runs as: NetworkManager starts dnsmasq
 * for a shared connection without --user, so it drops to dnsmasq's built-in
 * default, which Debian's dnsmasq-base leaves at "nobody". */
#define HELPER_DNS_USER "nobody"
#define HELPER_NFT "/usr/sbin/nft"
#define HELPER_IW "/usr/sbin/iw"
/* "aa:bb:cc:dd:ee:ff" */
#define MAC_TEXT_LENGTH 17
/* The longest script: the fixed lines plus HELPER_MAX_ADDRESSES addresses. */
#define SCRIPT_MAX 2048
/* The most output of "list" that is read. */
#define OUTPUT_MAX 65536

static const char kProgram[] = "lokictl-helper";

/* The table, set and chains, made when missing. "add" leaves what is
 * there, and the two chains are emptied and get their one rule again, so
 * the table always ends up in exactly this shape. */
static const char kEnsureTable[] =
    "add table inet lokictl\n"
    "add set inet lokictl blocked { type ether_addr; size " HELPER_SET_SIZE "; }\n"
    "add chain inet lokictl input { type filter hook input priority filter - 10; policy accept; }\n"
    "add chain inet lokictl forward { type filter hook forward priority filter - 10; policy accept; }\n"
    "flush chain inet lokictl input\n"
    "flush chain inet lokictl forward\n"
    "add rule inet lokictl input iifname " HELPER_HOTSPOTS " ether saddr @blocked drop\n"
    "add rule inet lokictl forward iifname " HELPER_HOTSPOTS " ether saddr @blocked drop\n";

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

static const char kKillOn[] =
    "add rule inet lokictl kill_forward iifname " HELPER_HOTSPOTS " oifname != \"" HELPER_TUNNEL "\" drop\n"
    "add rule inet lokictl kill_output meta skuid " HELPER_DNS_USER " oifname != { \"" HELPER_TUNNEL
    "\", \"lo\" } udp dport 53 drop\n"
    "add rule inet lokictl kill_output meta skuid " HELPER_DNS_USER " oifname != { \"" HELPER_TUNNEL
    "\", \"lo\" } tcp dport 53 drop\n";

static const char kKillOff[] =
    "delete chain inet lokictl kill_forward\n"
    "delete chain inet lokictl kill_output\n";

static void fail(const char* message) {
    fprintf(stderr, "%s: %s\n", kProgram, message);
}

static void usage(void) {
    fprintf(stderr,
            "usage: %s block MAC [MAC...] | unblock MAC [MAC...] | list | clear | ap-add | ap-del |\n"
            "       kill-on | kill-off\n"
            "  MAC: six pairs of hex digits joined by ':', at most %d per call\n",
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

/* Runs the program at `path` (nft or iw) with `arguments` (NULL-terminated,
 * fixed by this program), an empty environment, `input` on its standard
 * input and, when `output` is not NULL, its standard output read into
 * `output` (at most output_size - 1 bytes, ended by '\0'); otherwise its
 * standard output is dropped. Its messages go to this program's standard
 * error. Returns EXIT_DONE, EXIT_NFT_FAILED (it failed) or
 * EXIT_NFT_NOT_RUN (it could not be run). */
static int run_program(const char* path, char* const arguments[], const char* input, char* output,
                       size_t output_size) {
    char* const environment[] = {"LC_ALL=C", NULL};
    int in_pipe[2] = {-1, -1};
    int out_pipe[2] = {-1, -1};
    if (pipe2(in_pipe, O_CLOEXEC) != 0) {
        return EXIT_NFT_NOT_RUN;
    }
    if (output && pipe2(out_pipe, O_CLOEXEC) != 0) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        return EXIT_NFT_NOT_RUN;
    }

    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    posix_spawn_file_actions_init(&actions);
    posix_spawnattr_init(&attributes);
    posix_spawn_file_actions_adddup2(&actions, in_pipe[0], 0);
    if (output) {
        posix_spawn_file_actions_adddup2(&actions, out_pipe[1], 1);
    } else {
        posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
    }
    /* It starts with no blocked signals and SIGPIPE at its default. */
    sigset_t no_signals;
    sigset_t default_signals;
    sigemptyset(&no_signals);
    sigemptyset(&default_signals);
    sigaddset(&default_signals, SIGPIPE);
    posix_spawnattr_setsigmask(&attributes, &no_signals);
    posix_spawnattr_setsigdefault(&attributes, &default_signals);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);

    pid_t child = -1;
    const int spawned = posix_spawn(&child, path, &actions, &attributes, arguments, environment);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    close(in_pipe[0]);
    if (output) {
        close(out_pipe[1]);
    }
    if (spawned != 0) {
        close(in_pipe[1]);
        if (output) {
            close(out_pipe[0]);
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

    if (output) {
        size_t used = 0;
        for (;;) {
            char chunk[1024];
            const ssize_t got = read(out_pipe[0], chunk, sizeof(chunk));
            if (got < 0 && errno == EINTR) {
                continue;
            }
            if (got <= 0) {
                break;
            }
            const size_t room = output_size - 1 - used;
            const size_t take = (size_t)got < room ? (size_t)got : room;
            memcpy(output + used, chunk, take);
            used += take;
        }
        output[used] = '\0';
        close(out_pipe[0]);
    }

    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) {
            return EXIT_NFT_NOT_RUN;
        }
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
    int result = run_program(HELPER_NFT, tables, "", output, sizeof(output));
    if (result != EXIT_DONE) {
        return result;
    }
    /* Without the table nothing is blocked and the kill switch is off. */
    if (!has_line(output, "table inet lokictl")) {
        return EXIT_DONE;
    }
    char* const table[] = {"nft", "list", "table", "inet", "lokictl", NULL};
    result = run_program(HELPER_NFT, table, "", output, sizeof(output));
    if (result == EXIT_DONE) {
        if (has_line(output, "chain kill_forward {")) {
            printf("kill-switch\n");
        }
        /* The rules hold no hardware address, so every one is in the set. */
        print_addresses(output);
    }
    return result;
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
    const int result = run_program(HELPER_IW, add ? add_arguments : delete_arguments, "", NULL, 0);
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

int main(int argc, char* argv[]) {
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
    const int kill_on = strcmp(command, "kill-on") == 0;
    const int kill_off = strcmp(command, "kill-off") == 0;
    if (!block && !unblock && !list && !clear && !ap_add && !ap_del && !kill_on && !kill_off) {
        usage();
        return EXIT_USAGE;
    }

    /* Every argument is checked before anything runs. */
    unsigned char addresses[HELPER_MAX_ADDRESSES][6];
    int count = 0;
    if (block || unblock) {
        if (argc < 3 || argc - 2 > HELPER_MAX_ADDRESSES) {
            usage();
            return EXIT_USAGE;
        }
        for (int index = 2; index < argc; ++index) {
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

    char script[SCRIPT_MAX];
    size_t used = 0;
    script[0] = '\0';
    int built = 0;
    if (clear) {
        built = append(script, &used, kClearTable);
    } else if (kill_on || kill_off) {
        built = append(script, &used, kKillChains);
        if (built == 0) {
            built = append(script, &used, kill_on ? kKillOn : kKillOff);
        }
    } else {
        built = append(script, &used, kEnsureTable);
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

    char* const from_input[] = {"nft", "-f", "-", NULL};
    const int result = run_program(HELPER_NFT, from_input, script, NULL, 0);
    if (result == EXIT_NFT_NOT_RUN) {
        fail("could not run " HELPER_NFT);
    } else if (result == EXIT_NFT_FAILED) {
        fail("nft refused the change");
    }
    return result;
}
