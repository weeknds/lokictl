# SPDX-FileCopyrightText: 2026 LokiCTL contributors
# SPDX-License-Identifier: MIT
"""Device side of scripts/device.sh.

scripts/device.sh sends this file over SSH and runs it on the CardputerZero
as "python3 - <command> ...", so nothing besides the LokiCTL package is ever
installed on the device. It only uses the Python standard library and also
runs on older Python 3 versions (3.9 and newer).

Output rules:
  - Messages for people go to stderr.
  - stdout carries only the PNG data of "screen" and "snapshot", the plain
    text of "status", "stats" and "logs", and the name=value lines of
    "data".
  - Exit codes: 0 ok, 1 failed or refused, 2 usage error. Never 255, which
    device.sh reserves for "device unreachable".

Run "python3 scripts/device-agent.py selftest" on any computer to check the
parts that do not need the device.
"""

import argparse
import array
import contextlib
import fcntl
import hashlib
import io
import json
import os
import re
import signal
import struct
import subprocess
import sys
import tarfile
import tempfile
import time
import zlib

APP = "lokictl"
BINARY = "/usr/bin/lokictl"
DEV_UNIT = "lokictl-dev.service"
PACKAGED_UNIT = APP + ".service"
LAUNCHER_UNIT = "APPLaunch.service"
LAUNCHER_DIR = "/usr/share/APPLaunch"
DESKTOP_FILE = LAUNCHER_DIR + "/applications/" + APP + ".desktop"
KEYBOARD_NAME = "tca8418c"
FRAMEBUFFER = "/dev/fb0"
SCREEN_SIZE = (320, 170)
FRAME_STATS_VARIABLE = "LOKICTL_FRAME_STATS_FILE"
FRAME_STATS_NAME = APP + "-frame-stats"
PACKAGE_FILE_PATTERN = re.compile(r"^[A-Za-z0-9._+-]+\.deb$")

PROG = "device.sh"

# Linux input event constants (linux/input-event-codes.h).
EV_SYN = 0x00
EV_KEY = 0x01
SYN_REPORT = 0
FBIOGET_VSCREENINFO = 0x4600

_LETTER_CODES = {
    "q": 16, "w": 17, "e": 18, "r": 19, "t": 20, "y": 21, "u": 22, "i": 23,
    "o": 24, "p": 25, "a": 30, "s": 31, "d": 32, "f": 33, "g": 34, "h": 35,
    "j": 36, "k": 37, "l": 38, "z": 44, "x": 45, "c": 46, "v": 47, "b": 48,
    "n": 49, "m": 50,
}

# The only keys "key" can send. No raw codes and no free modifier
# combinations: the keyboard node also feeds the kernel's SysRq handler.
KEY_CODES = {
    "esc": 1,
    "1": 2, "2": 3, "3": 4, "4": 5, "5": 6, "6": 7, "7": 8, "8": 9, "9": 10,
    "0": 11,
    "backspace": 14,
    "tab": 15,
    "enter": 28,
    "space": 57,
    "screenshot": 99,  # KEY_SYSRQ, what the keyboard sends for fn+J
    "up": 103,
    "left": 105,
    "right": 106,
    "down": 108,
    "help": 138,  # KEY_HELP, what the keyboard sends for fn+H
}
KEY_CODES.update(_LETTER_CODES)

# The keyboard driver resolves the fn layer itself and reports these keys.
KEY_ALIASES = {
    "fn+h": "help",
    "fn+j": "screenshot",
    "fn+f": "up",
    "fn+x": "down",
    "fn+z": "left",
    "fn+c": "right",
}

DEFAULT_HOLD_MS = 60
LONG_HOLD_MS = 1000  # above the app's 700 ms long press, far below the launcher's 3 s
KEY_GAP_S = 0.15


class Failure(Exception):
    """A refusal or failure with a message for the user (exit code 1)."""


# ---------------------------------------------------------------------------
# Small helpers


def say(message):
    # A lost ssh connection must not stop the clean-up that follows a message
    # (such as bringing the launcher back), so write errors are ignored.
    try:
        sys.stderr.write(message + "\n")
        sys.stderr.flush()
    except (OSError, ValueError):
        pass


def run(cmd, timeout=60, stdout=subprocess.PIPE, stderr=subprocess.PIPE):
    """Runs a command without ever handing it this script's stdin."""
    if stdout is sys.stderr or stderr is sys.stderr:
        sys.stderr.flush()
    try:
        return subprocess.run(
            cmd,
            stdin=subprocess.DEVNULL,
            stdout=stdout,
            stderr=stderr,
            timeout=timeout,
            universal_newlines=True,
        )
    except FileNotFoundError:
        raise Failure("'%s' is not available on this device." % cmd[0])
    except subprocess.TimeoutExpired:
        raise Failure("'%s' did not finish within %d s." % (" ".join(cmd), timeout))


def output_of(cmd, timeout=60):
    result = run(cmd, timeout=timeout)
    return result.stdout.strip() if result.returncode == 0 else ""


def runtime_dir():
    return os.environ.get("XDG_RUNTIME_DIR") or "/run/user/%d" % os.getuid()


def frame_stats_path():
    return os.path.join(runtime_dir(), FRAME_STATS_NAME)


def remove_quietly(path):
    try:
        os.remove(path)
    except OSError:
        pass


def read_text(path):
    try:
        with open(path, "r") as handle:
            return handle.read()
    except OSError:
        return None


# ---------------------------------------------------------------------------
# systemd


def systemctl_user(*args, **kwargs):
    return run(["systemctl", "--user"] + list(args), **kwargs)


def unit_property(unit, name, user=True):
    cmd = ["systemctl"] + (["--user"] if user else []) + ["show", "-p", name, "--value", unit]
    return output_of(cmd)


def unit_exists(unit):
    return unit_property(unit, "LoadState") not in ("", "not-found")


def unit_state(unit):
    return unit_property(unit, "ActiveState") or "inactive"


def unit_main_pid(unit):
    try:
        return int(unit_property(unit, "MainPID") or "0")
    except ValueError:
        return 0


def launcher_installed():
    return unit_exists(LAUNCHER_UNIT)


# The unit name is reused for every run, so the journal holds all earlier runs
# too. Each run has its own invocation id: the app's lines carry it as
# _SYSTEMD_INVOCATION_ID and systemd's own lines about the unit (Started,
# Stopped, failures) as USER_INVOCATION_ID (INVOCATION_ID for system units).
INVOCATION_FIELDS = ("_SYSTEMD_INVOCATION_ID", "USER_INVOCATION_ID", "INVOCATION_ID")
INVOCATION_ID_PATTERN = re.compile(r"^[0-9a-f]{32}$")

# Where to look for the unit's lines: the user's journal first, then any
# journal this user may read.
JOURNAL_SCOPES = (["--user", "-u", DEV_UNIT], ["_SYSTEMD_USER_UNIT=" + DEV_UNIT])


def journal_since(timestamp):
    return ["--since", "@%.6f" % timestamp] if timestamp else []


def current_invocation_id():
    """The invocation id of the dev unit while systemd still has it loaded."""
    value = unit_property(DEV_UNIT, "InvocationID")
    return value if INVOCATION_ID_PATTERN.match(value) else ""


def latest_invocation_id(since=None):
    """The invocation id of the newest journal entry about the dev unit.

    Looks at the app's own lines and at systemd's lines, so a run that ended
    before printing anything still counts as the newest run.
    """
    for scope in JOURNAL_SCOPES:
        text = output_of(["journalctl"] + scope + journal_since(since)
                         + ["-r", "-n", "200", "--no-pager", "-o", "json",
                            "--output-fields=" + ",".join(INVOCATION_FIELDS)])
        for line in text.splitlines():
            try:
                entry = json.loads(line)
            except ValueError:
                continue
            for field in INVOCATION_FIELDS:
                value = entry.get(field)
                if isinstance(value, str) and INVOCATION_ID_PATTERN.match(value):
                    return value
    return ""


def journal_tail(lines, invocation="", since=None):
    """The last log lines of one run of the dev unit.

    invocation selects the run; without it, the newest run in the journal
    (newer than since, if given) is used. Only when no run can be told apart
    does it fall back to the unit's plain tail (limited to since, if given).
    """
    count = ["-n", str(lines), "--no-pager", "-o", "cat"]
    if not invocation:
        invocation = latest_invocation_id(since)
    if invocation:
        matches = []
        for field in INVOCATION_FIELDS:
            matches += (["+"] if matches else []) + [field + "=" + invocation]
        for scope in (["--user"], []):
            text = output_of(["journalctl"] + scope + matches + count)
            if text:
                return text
        # Never show another run instead of the one asked for.
        return ""
    for scope in JOURNAL_SCOPES:
        text = output_of(["journalctl"] + scope + journal_since(since) + count)
        if text:
            return text
    return ""


# ---------------------------------------------------------------------------
# Processes


def user_pids():
    uid = os.getuid()
    pids = []
    for entry in os.listdir("/proc"):
        if not entry.isdigit():
            continue
        try:
            if os.stat("/proc/" + entry).st_uid == uid:
                pids.append(int(entry))
        except OSError:
            continue
    return pids


def process_exe(pid):
    try:
        exe = os.readlink("/proc/%d/exe" % pid)
    except OSError:
        return ""
    # A program replaced by a reinstall shows up as "<path> (deleted)".
    if exe.endswith(" (deleted)"):
        exe = exe[: -len(" (deleted)")]
    return exe


def process_cmdline(pid):
    try:
        with open("/proc/%d/cmdline" % pid, "rb") as handle:
            raw = handle.read()
    except OSError:
        return []
    return [part.decode("utf-8", "replace") for part in raw.split(b"\0") if part]


def process_stat(pid):
    """Returns the fields of /proc/<pid>/stat from field 3 (state) onwards."""
    text = read_text("/proc/%d/stat" % pid)
    if not text:
        return None
    close = text.rfind(")")
    if close < 0:
        return None
    return text[close + 2:].split()


def process_comm(pid):
    text = read_text("/proc/%d/comm" % pid)
    return text.strip() if text else "?"


def process_alive(pid):
    if pid <= 0:
        return False
    fields = process_stat(pid)
    return bool(fields) and fields[0] not in ("Z", "X")


def lokictl_pids():
    """LokiCTL processes of this user, not counting snapshot runs."""
    pids = []
    for pid in user_pids():
        if process_exe(pid) != BINARY or not process_alive(pid):
            continue
        if "--snapshot" in process_cmdline(pid):
            continue
        pids.append(pid)
    return sorted(pids)


def backlight_level():
    """(brightness, max_brightness) of the screen's backlight, or None."""
    base = "/sys/class/backlight"
    try:
        names = sorted(os.listdir(base))
    except OSError:
        return None
    for name in names:
        level = read_text(os.path.join(base, name, "brightness"))
        maximum = read_text(os.path.join(base, name, "max_brightness"))
        if level and level.strip().isdigit():
            top = int(maximum) if maximum and maximum.strip().isdigit() else 0
            return int(level), top
    return None


def holds_fb(pid):
    fd_dir = "/proc/%d/fd" % pid
    try:
        names = os.listdir(fd_dir)
    except OSError:
        return False
    for name in names:
        try:
            if os.readlink(os.path.join(fd_dir, name)) == FRAMEBUFFER:
                return True
        except OSError:
            continue
    return False


def fb_holders():
    return [(pid, process_comm(pid)) for pid in user_pids() if holds_fb(pid)]


def process_unit(pid):
    """The systemd unit a process belongs to, read from its cgroup."""
    text = read_text("/proc/%d/cgroup" % pid) or ""
    for unit in (DEV_UNIT, LAUNCHER_UNIT):
        if "/" + unit in text:
            return unit
    return ""


def launcher_open_apps():
    """Apps the launcher has open: (pid, name, is_lokictl) for each.

    The launcher runs every app as its own child in a new process group, so
    an open app is a child of the launcher that leads its own group.
    """
    launcher_pid = unit_main_pid(LAUNCHER_UNIT) if launcher_installed() else 0
    if launcher_pid <= 0:
        return []
    stats = {}
    for pid in user_pids():
        fields = process_stat(pid)
        if fields and len(fields) > 2:
            stats[pid] = fields
    apps = []
    for pid, fields in stats.items():
        try:
            ppid, pgid = int(fields[1]), int(fields[2])
        except ValueError:
            continue
        if ppid != launcher_pid or pgid != pid:
            continue
        group = [other for other, other_fields in stats.items() if other_fields[2] == str(pgid)]
        is_lokictl = any(process_exe(member) == BINARY for member in group)
        apps.append((pid, process_comm(pid), is_lokictl))
    return apps


def describe_lokictl(pid):
    unit = process_unit(pid)
    if unit == DEV_UNIT:
        return "started by 'device.sh run'"
    if unit == LAUNCHER_UNIT:
        return "opened from the launcher"
    return "started some other way"


def wait_for(condition, timeout, interval=0.1):
    deadline = time.monotonic() + timeout
    while True:
        if condition():
            return True
        if time.monotonic() >= deadline:
            return False
        time.sleep(interval)


def read_frame_stats(path):
    text = read_text(path) if path else None
    if text is None:
        return None
    values = {}
    for line in text.splitlines():
        key, sep, value = line.partition("=")
        if sep:
            try:
                values[key.strip()] = int(value.strip())
            except ValueError:
                pass
    return values if "frames" in values else None


def process_frame_stats_path(pid):
    """The frame statistics file a running LokiCTL writes, if any."""
    try:
        with open("/proc/%d/environ" % pid, "rb") as handle:
            raw = handle.read()
    except OSError:
        return ""
    prefix = (FRAME_STATS_VARIABLE + "=").encode()
    for item in raw.split(b"\0"):
        if item.startswith(prefix):
            return item[len(prefix):].decode("utf-8", "replace")
    return ""


# ---------------------------------------------------------------------------
# Packages


def package_state():
    """(dpkg state, version); the state is "ii" when LokiCTL is installed.

    A removed package whose settings files remain still has a version, with
    state "rc", so the version alone does not mean installed.
    """
    text = output_of(["dpkg-query", "-W", "-f=${db:Status-Abbrev}|${Version}", APP])
    state, _, version = text.partition("|")
    return state.strip(), version.strip()


def installed_version():
    state, version = package_state()
    return version if state == "ii" else ""


def simulate(apt_args):
    """Runs an apt-get simulation; returns [(action, package)] for Inst/Remv lines."""
    result = run(["apt-get", "-s"] + apt_args, timeout=120)
    if result.returncode != 0:
        say(result.stdout.strip())
        say(result.stderr.strip())
        raise Failure("apt-get could not plan this change (see above).")
    changes = []
    for line in result.stdout.splitlines():
        match = re.match(r"^(Inst|Remv) (\S+)", line)
        if match:
            changes.append((match.group(1), match.group(2)))
    return changes


def sha256_of_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 16), b""):
            digest.update(block)
    return digest.hexdigest()


def sha256_in_package(package_path, member_suffix):
    """sha256 of one file inside a .deb, read with dpkg-deb --fsys-tarfile."""
    process = subprocess.Popen(
        ["dpkg-deb", "--fsys-tarfile", package_path],
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    digest = None
    try:
        with tarfile.open(fileobj=process.stdout, mode="r|*") as archive:
            for member in archive:
                if member.isfile() and member.name.endswith(member_suffix):
                    handle = archive.extractfile(member)
                    hasher = hashlib.sha256()
                    for block in iter(lambda: handle.read(1 << 16), b""):
                        hasher.update(block)
                    digest = hasher.hexdigest()
                    break
    finally:
        process.stdout.close()
        process.stderr.read()
        process.wait()
    return digest


def desktop_entry(path):
    entry = {}
    text = read_text(path)
    if text is None:
        return None
    section = ""
    for line in text.splitlines():
        line = line.strip()
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1]
        elif section == "Desktop Entry" and "=" in line and not line.startswith("#"):
            key, _, value = line.partition("=")
            entry.setdefault(key.strip(), value.strip())
    return entry


def system_unit_state(unit):
    """(is-enabled, is-active) of a system unit; both exit non-zero when off."""
    enabled = run(["systemctl", "is-enabled", unit]).stdout.strip() or "not found"
    active = run(["systemctl", "is-active", unit]).stdout.strip() or "unknown"
    return enabled, active


def cmd_install(args):
    name = os.path.basename(args.package)
    if not PACKAGE_FILE_PATTERN.match(name):
        raise Failure("'%s' does not look like a package file name." % name)
    path = os.path.abspath(args.package)
    if os.path.islink(path) or not os.path.isfile(path):
        raise Failure("%s was not found on the device." % path)

    # From here on the uploaded file is ours, and it is deleted on every path.
    try:
        package = output_of(["dpkg-deb", "-f", path, "Package"])
        if package != APP:
            raise Failure("%s is not a %s package (Package: %s)."
                          % (args.label or name, APP, package or "?"))
        version = output_of(["dpkg-deb", "-f", path, "Version"])

        if args.sha256:
            actual = sha256_of_file(path)
            if actual != args.sha256.lower():
                raise Failure("The copy on the device is damaged (checksum mismatch). Deploy again.")

        apt_options = ["--reinstall", "--allow-downgrades", "--no-install-recommends"]
        changes = simulate(["install"] + apt_options + [path])
        others = [(action, pkg) for action, pkg in changes if pkg != APP]
        removals = [pkg for action, pkg in others if action == "Remv"]
        if removals:
            raise Failure("Installing would remove other packages (%s). Nothing was changed."
                          % ", ".join(removals))
        if others and not args.with_deps:
            raise Failure("Installing would also install: %s. Nothing was changed. Run "
                          "'device.sh deploy --with-deps' to allow that."
                          % ", ".join(pkg for _, pkg in others))

        say("Installing %s (%s %s) ..." % (args.label or name, APP, version or "?"))
        result = run(
            ["sudo", "-n", "env", "DEBIAN_FRONTEND=noninteractive", "apt-get", "install", "-y"]
            + apt_options
            + ["-o", "Dpkg::Options::=--force-confold", "-o", "DPkg::Lock::Timeout=60", path],
            timeout=600,
            stdout=sys.stderr,
            stderr=sys.stderr,
        )
        if result.returncode != 0:
            raise Failure("apt-get install failed (exit %d, see above). Installing needs "
                          "passwordless sudo for this user." % result.returncode)

        expected = sha256_in_package(path, "usr/bin/" + APP)
        if not expected:
            raise Failure("The package has no usr/bin/%s." % APP)
        actual = sha256_of_file(BINARY) if os.path.isfile(BINARY) else ""
        if actual != expected:
            raise Failure("%s does not match the program in the package." % BINARY)

        entry = desktop_entry(DESKTOP_FILE)
        if entry is None:
            raise Failure("The launcher entry %s is missing." % DESKTOP_FILE)
        exec_line = entry.get("Exec", "").split()
        exec_path = exec_line[0] if exec_line else ""
        if not exec_path or not os.path.isfile(exec_path) or not os.access(exec_path, os.X_OK):
            raise Failure("The launcher entry's Exec (%s) is not a program." % (exec_path or "empty"))
        icon = entry.get("Icon", "")
        icon_path = icon if icon.startswith("/") else os.path.join(LAUNCHER_DIR, icon)
        if not icon or not os.path.isfile(icon_path):
            raise Failure("The launcher icon %s is missing." % (icon_path or "(none)"))

        enabled, active = system_unit_state(PACKAGED_UNIT)
        if enabled == "enabled" or active == "active":
            raise Failure("The packaged %s is %s/%s; it must stay off (it would run the app "
                          "as root)." % (PACKAGED_UNIT, enabled, active))
    finally:
        remove_quietly(path)

    say("Installed %s %s." % (APP, installed_version() or "?"))
    say("The installed %s matches the package (sha256 %s...)." % (BINARY, expected[:16]))
    say("LokiCTL is now on the launcher's HOME screen (%s)." % os.path.basename(DESKTOP_FILE))
    running = lokictl_pids()
    if running:
        say("LokiCTL is still running the old program (pid %s) until it is restarted: "
            "device.sh stop, then device.sh run." % ", ".join(str(pid) for pid in running))
    return 0


def cmd_remove(args):
    for item in stop_everything():
        say("- " + item)
    if not installed_version():
        say("LokiCTL is not installed; nothing to remove.")
        return 0
    changes = simulate(["remove", APP])
    others = [pkg for _, pkg in changes if pkg != APP]
    if others:
        raise Failure("Removing would also change: %s. Nothing was changed." % ", ".join(others))
    result = run(
        ["sudo", "-n", "env", "DEBIAN_FRONTEND=noninteractive", "apt-get", "remove", "-y",
         "-o", "DPkg::Lock::Timeout=60", APP],
        timeout=300,
        stdout=sys.stderr,
        stderr=sys.stderr,
    )
    if result.returncode != 0:
        raise Failure("apt-get remove failed (exit %d, see above)." % result.returncode)
    say("LokiCTL was removed. Its settings in /etc stay until the package is purged.")
    return 0


# ---------------------------------------------------------------------------
# run / stop / status


def start_launcher_and_wait(timeout=15.0):
    """Starts the launcher if needed and waits until it has the screen."""
    if unit_state(LAUNCHER_UNIT) != "active":
        result = systemctl_user("start", LAUNCHER_UNIT, timeout=60)
        if result.returncode != 0:
            say(result.stderr.strip())
    ok = wait_for(lambda: holds_fb(unit_main_pid(LAUNCHER_UNIT)), timeout, 0.25)
    if ok:
        time.sleep(2.0)  # give it time to draw HOME
    return ok


def dev_unit_command(stats_path, launcher, theme=None, data=None):
    """The systemd-run command line that starts LokiCTL for 'run'.

    theme, if given, is passed on as "--theme <theme>" after the program
    path, and data, if given, as "--data <data>" after that. Neither is
    checked here: LokiCTL knows its theme and data source names and exits at
    once with a message if it does not know one. Without data, LokiCTL on
    the device reads live values.
    """
    cmd = ["systemd-run", "--user", "--unit=" + DEV_UNIT, "--collect", "--quiet",
           "-p", "TimeoutStopSec=5",
           "--setenv=%s=%s" % (FRAME_STATS_VARIABLE, stats_path)]
    if launcher:
        # Whenever LokiCTL ends (stop, crash, or the user quits it on the
        # device), systemd brings the launcher back.
        cmd += ["--working-directory=" + LAUNCHER_DIR,
                "-p", "ExecStopPost=/usr/bin/systemctl --user start --no-block " + LAUNCHER_UNIT]
    # systemd-run stops reading its own options at the program path, so
    # everything after it goes to LokiCTL.
    cmd.append(BINARY)
    if theme is not None:
        cmd += ["--theme", theme]
    if data is not None:
        cmd += ["--data", data]
    return cmd


def cmd_run(args):
    if not os.path.isfile(BINARY):
        raise Failure("LokiCTL is not installed. Run 'device.sh deploy' first.")
    running = lokictl_pids()
    if running:
        raise Failure("LokiCTL is already running (pid %s, %s). Use 'device.sh stop' first."
                      % (running[0], describe_lokictl(running[0])))

    def other_apps():
        return [(pid, name) for pid, name, is_lokictl in launcher_open_apps() if not is_lokictl]

    # Right after it starts, the launcher runs short helpers (such as nmcli)
    # the same way it runs apps, so only refuse if the other program stays.
    if other_apps() and not wait_for(lambda: not other_apps(), 3.0, 0.25):
        pid, name = other_apps()[0] if other_apps() else (0, "?")
        raise Failure("'%s' is open on the device (pid %d). Close it there first; "
                      "nothing was changed." % (name, pid))

    launcher = launcher_installed()
    stats_path = frame_stats_path()
    started = False
    attempt_started_at = None  # set just before systemd-run
    invocation = ""
    try:
        if launcher:
            say("Pausing the launcher (%s) while LokiCTL runs ..." % LAUNCHER_UNIT)
            result = systemctl_user("stop", LAUNCHER_UNIT, timeout=45)
            if result.returncode != 0:
                say(result.stderr.strip())
                raise Failure("Could not pause the launcher.")
        systemctl_user("reset-failed", DEV_UNIT)
        remove_quietly(stats_path)
        remove_quietly(stats_path + ".tmp")

        cmd = dev_unit_command(stats_path, launcher, args.theme, args.data)
        attempt_started_at = time.time()
        result = run(cmd, timeout=30)
        if result.returncode != 0:
            say(result.stderr.strip())
            raise Failure("systemd-run could not start LokiCTL.")
        # Identifies this run's lines in the journal. Empty if the unit has
        # already ended and been unloaded; the log tail then goes by time.
        invocation = current_invocation_id()

        def has_screen():
            pid = unit_main_pid(DEV_UNIT)
            return process_alive(pid) and holds_fb(pid)

        def has_ended():
            # "deactivating" means the program is gone and systemd is running
            # the clean-up (ExecStopPost); an unloaded unit reads "inactive".
            return unit_state(DEV_UNIT) not in ("active", "activating", "reloading")

        # LokiCTL exits at once when it refuses its options (an unknown
        # --theme, for example), so stop waiting as soon as the unit ends.
        if not wait_for(lambda: has_screen() or has_ended(), 5.0):
            raise Failure("LokiCTL did not open %s within 5 s." % FRAMEBUFFER)
        if not has_screen():
            raise Failure("LokiCTL did not start: it exited right away. Its own message is "
                          "in the log lines above.")
        pid = unit_main_pid(DEV_UNIT)

        def drew_frame():
            values = read_frame_stats(stats_path)
            return bool(values) and values.get("frames", 0) > 0

        if not wait_for(drew_frame, 8.0):
            say("Warning: LokiCTL has not reported a finished frame yet.")
        started = True
    finally:
        if not started:
            if unit_state(DEV_UNIT) in ("active", "activating"):
                systemctl_user("stop", DEV_UNIT, timeout=30)
            if launcher:
                say("Restoring the launcher ...")
                start_launcher_and_wait()
            if attempt_started_at is not None:
                # Only this attempt's lines, never an earlier run's. When
                # LokiCTL refuses its options it prints its message and then
                # its usage text (about 17 lines), so keep enough lines to
                # still show the message.
                try:
                    tail = journal_tail(40, invocation, since=attempt_started_at)
                except Failure:
                    tail = ""
                if tail:
                    say("Last log lines of %s:\n%s" % (DEV_UNIT, tail))

    say("LokiCTL is running (pid %d)." % pid)
    if launcher:
        say("The launcher is paused until 'device.sh stop', or until LokiCTL exits.")
    level = backlight_level()
    if level and level[0] == 0:
        say("Note: the backlight is off (the launcher had put the screen to sleep), so the "
            "panel stays dark; 'screen' still captures what LokiCTL draws. To see it on the "
            "device, stop, press a key on the device to wake it, then run again.")
    return 0


def stop_everything():
    """Stops every LokiCTL run and makes sure the launcher is back.

    Returns a list of what was done (empty if nothing was running).
    """
    done = []
    launcher = launcher_installed()
    launcher_was_active = launcher and unit_state(LAUNCHER_UNIT) == "active"
    if unit_state(DEV_UNIT) in ("active", "activating", "deactivating", "reloading"):
        result = systemctl_user("stop", DEV_UNIT, timeout=30)
        if result.returncode != 0:
            say(result.stderr.strip())
        done.append("stopped LokiCTL (%s)" % DEV_UNIT)

    leftover = lokictl_pids()
    if leftover:
        for pid in leftover:
            try:
                os.kill(pid, signal.SIGTERM)
            except OSError:
                pass
        if not wait_for(lambda: not lokictl_pids(), 5.0):
            for pid in lokictl_pids():
                try:
                    os.kill(pid, signal.SIGKILL)
                except OSError:
                    pass
            wait_for(lambda: not lokictl_pids(), 2.0)
            done.append("killed LokiCTL (pid %s) after it ignored SIGTERM"
                        % ", ".join(str(pid) for pid in leftover))
        else:
            done.append("stopped LokiCTL (pid %s)" % ", ".join(str(pid) for pid in leftover))

    if not lokictl_pids():
        stats_path = frame_stats_path()
        remove_quietly(stats_path)
        remove_quietly(stats_path + ".tmp")

    if launcher and (done or not launcher_was_active):
        # stop above may already have brought the launcher back (ExecStopPost);
        # starting it again is harmless.
        if not start_launcher_and_wait():
            say("Warning: the launcher has not taken over the screen yet.")
        if not launcher_was_active:
            done.append("brought the launcher back")
    return done


def cmd_stop(args):
    done = stop_everything()
    problems = []
    if lokictl_pids():
        problems.append("a LokiCTL process is still running")
    if launcher_installed() and unit_state(LAUNCHER_UNIT) != "active":
        problems.append("the launcher is not active")
    for item in done:
        say("- " + item)
    if not done:
        say("Nothing was running; the launcher is active.")
    if problems:
        raise Failure("Stop did not finish: " + "; ".join(problems) + ".")
    if done:
        say("LokiCTL is stopped and the launcher is back on HOME.")
    return 0


def cmd_status(args):
    lines = []
    state, version = package_state()
    if state == "ii":
        lines.append(("package", "%s %s" % (APP, version)))
    elif state.startswith("rc"):
        lines.append(("package", "not installed (settings in /etc remain)"))
    elif state:
        lines.append(("package", "%s %s, dpkg state %s (not fully installed)" % (APP, version, state)))
    else:
        lines.append(("package", "not installed"))

    running = lokictl_pids()
    if running:
        lines.append(("lokictl", "; ".join("running, pid %d, %s" % (pid, describe_lokictl(pid))
                                           for pid in running)))
    else:
        lines.append(("lokictl", "not running"))

    if launcher_installed():
        state = unit_state(LAUNCHER_UNIT)
        pid = unit_main_pid(LAUNCHER_UNIT)
        if state == "active":
            lines.append(("launcher", "active (pid %d)" % pid))
        else:
            lines.append(("launcher", state + (" (paused by device.sh run)" if running else "")))
        open_apps = [name for _, name, is_lokictl in launcher_open_apps() if not is_lokictl]
        if open_apps:
            lines.append(("open in launcher", ", ".join(open_apps)))
    else:
        lines.append(("launcher", "not installed"))

    level = backlight_level()
    if level:
        lines.append(("backlight", ("off (0 of %d)" % level[1]) if level[0] == 0
                      else "%d of %d" % level))
    holders = fb_holders()
    lines.append((FRAMEBUFFER, ", ".join("%s (%d)" % (name, pid) for pid, name in holders)
                  if holders else "not open"))

    enabled, active = system_unit_state(PACKAGED_UNIT)
    lines.append((PACKAGED_UNIT, "%s, %s (packaged system unit; should stay off)" % (enabled, active)))
    print_table(lines)
    return 0


def print_table(lines):
    width = max(len(label) for label, _ in lines) + 2
    for label, value in lines:
        sys.stdout.write(("%s:" % label).ljust(width) + value + "\n")
    sys.stdout.flush()


# ---------------------------------------------------------------------------
# screen


def channel_table(length):
    """Expands a channel of `length` bits to 8 bits by bit replication."""
    if length <= 0:
        return [0]
    table = []
    for value in range(1 << length):
        out, bits = value, length
        while bits < 8:
            out = (out << length) | value
            bits += length
        table.append((out >> (bits - 8)) & 0xFF)
    return table


def rgb_rows(data, width, height, stride, bpp, fields):
    """Turns raw framebuffer rows into 8-bit RGB rows.

    fields: ((offset, length), (offset, length), (offset, length)) for R, G, B.
    """
    tables = [channel_table(length) for _, length in fields]
    masks = [(1 << length) - 1 for _, length in fields]
    shifts = [offset for offset, _ in fields]
    rows = []
    if bpp == 16:
        rt, gt, bt = tables
        pixel_rows = []
        for y in range(height):
            pixels = array.array("H", data[y * stride: y * stride + width * 2])
            if sys.byteorder == "big":
                pixels.byteswap()
            pixel_rows.append(pixels)
        # A screen has few distinct colours, so convert each one only once.
        colours = set()
        for pixels in pixel_rows:
            colours.update(pixels)
        lut = {v: bytes((rt[(v >> shifts[0]) & masks[0]],
                         gt[(v >> shifts[1]) & masks[1]],
                         bt[(v >> shifts[2]) & masks[2]])) for v in colours}
        for pixels in pixel_rows:
            rows.append(b"".join([lut[v] for v in pixels]))
    elif bpp == 32:
        for y in range(height):
            pixels = array.array("I" if array.array("I").itemsize == 4 else "L",
                                 data[y * stride: y * stride + width * 4])
            if sys.byteorder == "big":
                pixels.byteswap()
            row = bytearray(width * 3)
            for x, v in enumerate(pixels):
                row[x * 3] = tables[0][(v >> shifts[0]) & masks[0]]
                row[x * 3 + 1] = tables[1][(v >> shifts[1]) & masks[1]]
                row[x * 3 + 2] = tables[2][(v >> shifts[2]) & masks[2]]
            rows.append(bytes(row))
    else:
        raise Failure("Unsupported framebuffer depth: %d bits per pixel." % bpp)
    return rows


def png_bytes(width, height, rows):
    """Encodes 8-bit RGB rows as a PNG (no interlacing, filter 0)."""
    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload
                + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    raw = b"".join(b"\x00" + row for row in rows)
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
            + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def png_decode_rgb(data):
    """Decodes a PNG made by png_bytes (used by selftest)."""
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos, idat, width, height = 8, b"", 0, 0
    while pos < len(data):
        length, tag = struct.unpack(">I4s", data[pos:pos + 8])
        payload = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if tag == b"IHDR":
            width, height, depth, colour = struct.unpack(">IIBB", payload[:10])
            if depth != 8 or colour != 2:
                raise ValueError("unexpected PNG format")
        elif tag == b"IDAT":
            idat += payload
    raw = zlib.decompress(idat)
    stride = width * 3
    rows = []
    for y in range(height):
        start = y * (stride + 1)
        if raw[start] != 0:
            raise ValueError("unexpected PNG filter")
        rows.append(raw[start + 1:start + 1 + stride])
    return width, height, rows


def framebuffer_info(handle):
    buffer = bytearray(160)
    fcntl.ioctl(handle.fileno(), FBIOGET_VSCREENINFO, buffer, True)
    (xres, yres, _xvirt, _yvirt, xoffset, yoffset, bpp, _gray) = struct.unpack_from("=8I", buffer, 0)
    red = struct.unpack_from("=3I", buffer, 32)
    green = struct.unpack_from("=3I", buffer, 44)
    blue = struct.unpack_from("=3I", buffer, 56)
    return xres, yres, xoffset, yoffset, bpp, (red, green, blue)


def read_exact(handle, offset, size):
    handle.seek(offset)
    chunks, remaining = [], size
    while remaining > 0:
        chunk = handle.read(remaining)
        if not chunk:
            break
        chunks.append(chunk)
        remaining -= len(chunk)
    data = b"".join(chunks)
    if len(data) != size:
        raise Failure("Could only read %d of %d bytes from %s." % (len(data), size, FRAMEBUFFER))
    return data


def cmd_screen(args):
    try:
        handle = open(FRAMEBUFFER, "rb", buffering=0)
    except OSError as error:
        raise Failure("Cannot open %s: %s (the user needs the 'video' group)."
                      % (FRAMEBUFFER, error.strerror))
    with handle:
        xres, yres, xoffset, yoffset, bpp, colour_fields = framebuffer_info(handle)
        if (xres, yres) != SCREEN_SIZE:
            raise Failure("%s is %dx%d, expected %dx%d." % ((FRAMEBUFFER, xres, yres) + SCREEN_SIZE))
        if bpp not in (16, 32):
            raise Failure("%s uses %d bits per pixel; only 16 and 32 are supported."
                          % (FRAMEBUFFER, bpp))
        if any(msb_right for _, _, msb_right in colour_fields):
            raise Failure("%s stores colour bits in reverse order, which is not supported."
                          % FRAMEBUFFER)
        stride_text = read_text("/sys/class/graphics/%s/stride" % os.path.basename(FRAMEBUFFER))
        stride = int(stride_text) if stride_text and stride_text.strip().isdigit() else xres * bpp // 8
        offset = yoffset * stride + xoffset * bpp // 8
        size = stride * yres

        data = read_exact(handle, offset, size)
        if not args.no_wait:
            # Read again until two reads match, so the picture is never taken
            # in the middle of a redraw.
            deadline = time.monotonic() + 2.0
            stable = False
            while time.monotonic() < deadline:
                time.sleep(0.12)
                again = read_exact(handle, offset, size)
                if again == data:
                    stable = True
                    break
                data = again
            if not stable:
                say("The screen kept changing; saved the latest picture.")

    fields = tuple((offset_bits, length) for offset_bits, length, _ in colour_fields)
    rows = rgb_rows(data, xres, yres, stride, bpp, fields)
    sys.stdout.buffer.write(png_bytes(xres, yres, rows))
    sys.stdout.buffer.flush()
    return 0


# ---------------------------------------------------------------------------
# key


def resolve_key(name):
    lowered = name.lower()
    lowered = KEY_ALIASES.get(lowered, lowered)
    return KEY_CODES.get(lowered)


def key_names_text():
    plain = sorted(KEY_CODES, key=lambda k: (len(k) > 1, k))
    return ("keys: " + " ".join(plain) + "\naliases: "
            + " ".join("%s=%s" % item for item in sorted(KEY_ALIASES.items())))


def parse_key_capabilities(text):
    """Parses a sysfs capabilities/key bitmap into a set of key codes.

    The kernel prints the bitmap as hex words, most significant first, each
    as wide as an unsigned long (64 bits on arm64), leaving out leading zero
    words.
    """
    words = text.split()
    word_bits = 64 if struct.calcsize("L") == 8 or any(len(w) > 8 for w in words) else 32
    codes = set()
    for index, word in enumerate(reversed(words)):
        value = int(word, 16)
        bit = 0
        while value:
            if value & 1:
                codes.add(index * word_bits + bit)
            value >>= 1
            bit += 1
    return codes


def find_keyboard():
    base = "/sys/class/input"
    try:
        entries = sorted(os.listdir(base))
    except OSError:
        entries = []
    for entry in entries:
        if not entry.startswith("event"):
            continue
        name = read_text(os.path.join(base, entry, "device", "name"))
        if name and name.strip() == KEYBOARD_NAME:
            caps = read_text(os.path.join(base, entry, "device", "capabilities", "key")) or ""
            return "/dev/input/" + entry, parse_key_capabilities(caps)
    raise Failure("No input device named '%s' was found." % KEYBOARD_NAME)


def cmd_key(args):
    codes = []
    unknown = []
    for name in args.keys:
        code = resolve_key(name)
        if code is None:
            unknown.append(name)
        else:
            codes.append((name, code))
    if unknown:
        say("%s key: unknown key name(s): %s" % (PROG, ", ".join(unknown)))
        say(key_names_text())
        return 2

    if args.hold_ms is not None:
        hold_ms = args.hold_ms
    elif args.long:
        hold_ms = LONG_HOLD_MS
    else:
        hold_ms = DEFAULT_HOLD_MS

    if not args.launcher and not lokictl_pids():
        raise Failure("LokiCTL is not running, so the keys would go to the launcher. "
                      "Nothing was sent. Start it with 'device.sh run' (or add --launcher "
                      "to really press keys on the launcher).")

    node, supported = find_keyboard()
    missing = [name for name, code in codes if code not in supported]
    if missing:
        raise Failure("The keyboard cannot send: %s. Nothing was sent." % ", ".join(missing))

    try:
        fd = os.open(node, os.O_WRONLY | getattr(os, "O_CLOEXEC", 0))
    except PermissionError:
        raise Failure("Cannot write to %s: this user must be in the 'input' group." % node)

    def event(event_type, code, value):
        return struct.pack("@llHHi", 0, 0, event_type, code, value)

    try:
        for index, (name, code) in enumerate(codes):
            if index:
                time.sleep(KEY_GAP_S)
            os.write(fd, event(EV_KEY, code, 1) + event(EV_SYN, SYN_REPORT, 0))
            try:
                time.sleep(hold_ms / 1000.0)
            finally:
                # Always release, so a key never stays down.
                os.write(fd, event(EV_KEY, code, 0) + event(EV_SYN, SYN_REPORT, 0))
            say("pressed %s (%d ms)" % (name, hold_ms))
    finally:
        os.close(fd)
    return 0


# ---------------------------------------------------------------------------
# stats


def proc_status_value(pid, key):
    text = read_text("/proc/%d/status" % pid) or ""
    for line in text.splitlines():
        if line.startswith(key + ":"):
            return line.split(":", 1)[1].strip()
    return ""


def kb_value(text):
    try:
        return int(text.split()[0])
    except (ValueError, IndexError):
        return None


def meminfo():
    values = {}
    for line in (read_text("/proc/meminfo") or "").splitlines():
        key, _, rest = line.partition(":")
        number = kb_value(rest)
        if number is not None:
            values[key] = number
    return values


def cpu_ticks(pid):
    fields = process_stat(pid)
    if not fields or len(fields) < 20:
        return None
    return int(fields[11]) + int(fields[12])


def format_duration(seconds):
    seconds = int(seconds)
    hours, rest = divmod(seconds, 3600)
    minutes, secs = divmod(rest, 60)
    return "%d:%02d:%02d" % (hours, minutes, secs)


def cmd_stats(args):
    running = lokictl_pids()
    if not running:
        raise Failure("LokiCTL is not running. Start it with 'device.sh run'.")
    dev_pid = unit_main_pid(DEV_UNIT)
    pid = dev_pid if dev_pid in running else running[0]
    ticks_per_second = os.sysconf("SC_CLK_TCK")
    stats_path = process_frame_stats_path(pid)

    frames_before = read_frame_stats(stats_path)
    ticks_before = cpu_ticks(pid)
    started = time.monotonic()
    time.sleep(args.seconds)
    ticks_after = cpu_ticks(pid)
    elapsed = time.monotonic() - started
    frames_after = read_frame_stats(stats_path)
    if ticks_before is None or ticks_after is None:
        raise Failure("LokiCTL (pid %d) stopped during the measurement." % pid)

    fields = process_stat(pid)
    uptime_text = (read_text("/proc/uptime") or "0").split()[0]
    process_age = float(uptime_text) - int(fields[19]) / float(ticks_per_second)
    rss_kb = kb_value(proc_status_value(pid, "VmRSS")) or 0
    threads = proc_status_value(pid, "Threads") or "?"
    pss_kb = None
    for line in (read_text("/proc/%d/smaps_rollup" % pid) or "").splitlines():
        if line.startswith("Pss:"):
            pss_kb = kb_value(line.split(":", 1)[1])
            break
    cpu_percent = (ticks_after - ticks_before) / float(ticks_per_second) / elapsed * 100.0
    memory = meminfo()
    load = " ".join((read_text("/proc/loadavg") or "? ? ?").split()[:3])

    lines = [("pid", "%d (%s, running for %s)" % (pid, describe_lokictl(pid), format_duration(process_age)))]
    memory_text = "RSS %.1f MB" % (rss_kb / 1024.0)
    if pss_kb is not None:
        memory_text += ", PSS %.1f MB" % (pss_kb / 1024.0)
    memory_text += ", %s threads" % threads
    lines.append(("memory", memory_text))
    lines.append(("cpu", "%.1f %% of one core over %.1f s" % (cpu_percent, elapsed)))
    if "MemAvailable" in memory and "MemTotal" in memory:
        swap_used = memory.get("SwapTotal", 0) - memory.get("SwapFree", 0)
        lines.append(("system memory", "%d MB available of %d MB, swap used %d MB" % (
            memory["MemAvailable"] // 1024, memory["MemTotal"] // 1024, swap_used // 1024)))
    lines.append(("load", load))

    if frames_after is None:
        lines.append(("frame time", "not measured (start LokiCTL with 'device.sh run')"))
    else:
        total = frames_after.get("frames", 0)
        window = total - frames_before.get("frames", 0) if frames_before else 0
        if window > 0:
            spent = frames_after.get("render_us_total", 0) - frames_before.get("render_us_total", 0)
            average = "avg %.1f ms over the window" % (spent / 1000.0 / window)
        elif total > 0:
            average = "avg %.1f ms since start" % (frames_after.get("render_us_total", 0) / 1000.0 / total)
        else:
            average = "no frames yet"
        lines.append(("frame time", "%s, max %.1f ms, last %.1f ms; %d frames in total, %d in the window"
                      % (average, frames_after.get("render_us_max", 0) / 1000.0,
                         frames_after.get("render_us_last", 0) / 1000.0, total, window)))
    print_table(lines)
    return 0


# ---------------------------------------------------------------------------
# snapshot / logs


def snapshot_command(path, args):
    """The "lokictl --snapshot" command line for the "snapshot" arguments.

    The options follow in the order --page, --theme, --data, --data-steps,
    --keys, --switch-to, --settle-ms, each only when it was given. --page,
    --theme, --data, --keys and --switch-to are passed on unchecked;
    LokiCTL knows its page, theme, data source and key names and exits with
    a message for an unknown one. The keys are pressed inside that snapshot
    run, not on the device's keyboard; the name "data" among them takes one
    further reading at that point. Without --data the picture shows the
    fixed sample values.
    """
    cmd = [BINARY, "--snapshot", path]
    if args.page:
        cmd += ["--page", args.page]
    if args.theme:
        cmd += ["--theme", args.theme]
    if args.data is not None:
        cmd += ["--data", args.data]
    if args.data_steps is not None:
        cmd += ["--data-steps", str(args.data_steps)]
    if args.keys:
        cmd += ["--keys", args.keys]
    if args.switch_to:
        cmd += ["--switch-to", args.switch_to]
    if args.settle_ms is not None:
        cmd += ["--settle-ms", str(args.settle_ms)]
    return cmd


def cmd_snapshot(args):
    if not os.path.isfile(BINARY):
        raise Failure("LokiCTL is not installed. Run 'device.sh deploy' first.")
    base = runtime_dir()
    if not os.path.isdir(base):
        base = "/tmp"
    folder = tempfile.mkdtemp(prefix=APP + "-snapshot-", dir=base)
    path = os.path.join(folder, "snapshot.png")
    try:
        cmd = snapshot_command(path, args)
        # The app logs to stdout, which is reserved for the picture here. Its
        # standard error (the "fonts loaded" lines of --switch-to or the
        # "quit requested" line of --keys, for example) goes to the caller's
        # standard error as well.
        result = run(cmd, timeout=60, stdout=sys.stderr, stderr=sys.stderr)
        if result.returncode != 0:
            say("lokictl --snapshot failed (exit %d)." % result.returncode)
            return result.returncode if 0 < result.returncode < 255 else 1
        with open(path, "rb") as handle:
            sys.stdout.buffer.write(handle.read())
        sys.stdout.buffer.flush()
        return 0
    finally:
        remove_quietly(path)
        try:
            os.rmdir(folder)
        except OSError:
            pass


def data_command(args):
    """The "lokictl --print-data" command line for the "data" arguments.

    --data, if given, is passed on unchecked: LokiCTL knows its data source
    names and exits with code 2 and a message for an unknown one.
    """
    cmd = [BINARY, "--print-data"]
    if args.data is not None:
        cmd += ["--data", args.data]
    return cmd


def cmd_data(args):
    # --print-data reads no settings, opens no window and touches neither the
    # screen nor the keyboard, so it does not matter whether LokiCTL or the
    # launcher is running, and neither is touched.
    if not os.path.isfile(BINARY):
        raise Failure("LokiCTL is not installed. Run 'device.sh deploy' first.")
    sys.stdout.flush()
    # The name=value lines go straight to the caller's standard output and
    # LokiCTL's messages to standard error.
    result = run(data_command(args), timeout=60, stdout=sys.stdout, stderr=sys.stderr)
    if result.returncode != 0:
        say("lokictl --print-data failed (exit %d)." % result.returncode)
        return result.returncode if 0 < result.returncode < 255 else 1
    return 0


def cmd_logs(args):
    # The latest run only: the one systemd still has loaded, or else the
    # newest one in the journal.
    text = journal_tail(args.lines, current_invocation_id())
    if text:
        sys.stdout.write(text + "\n")
        sys.stdout.flush()
    else:
        say("No log lines for %s yet (it has not been started with 'device.sh run')." % DEV_UNIT)
    return 0


# ---------------------------------------------------------------------------
# selftest (runs anywhere, touches no device)


def cmd_selftest(args):
    width, height = SCREEN_SIZE
    stride = width * 2 + 64  # padded rows, like some framebuffers
    raw = bytearray(stride * height)
    expected = {}
    for y in range(height):
        for x in range(width):
            r5, g6, b5 = (x * 31) // (width - 1), (y * 63) // (height - 1), (x + y) % 32
            value = (r5 << 11) | (g6 << 5) | b5
            struct.pack_into("<H", raw, y * stride + x * 2, value)
            if (x, y) in ((0, 0), (width - 1, 0), (0, height - 1), (width - 1, height - 1), (123, 45)):
                expected[(x, y)] = (r5, g6, b5)
    rows = rgb_rows(bytes(raw), width, height, stride, 16, ((11, 5), (5, 6), (0, 5)))
    png = png_bytes(width, height, rows)
    assert png[:8] == b"\x89PNG\r\n\x1a\n"
    assert struct.unpack(">II", png[16:24]) == (width, height), "PNG header size"
    w, h, decoded = png_decode_rgb(png)
    assert (w, h) == (width, height)
    t5, t6 = channel_table(5), channel_table(6)
    assert (t5[0], t5[31], t6[0], t6[63], t5[1]) == (0, 255, 0, 255, 8), "bit replication"
    for (x, y), (r5, g6, b5) in expected.items():
        pixel = tuple(decoded[y][x * 3:x * 3 + 3])
        assert pixel == (t5[r5], t6[g6], t5[b5]), "pixel %s: %s" % ((x, y), pixel)

    # 32 bpp, XRGB8888
    raw32 = bytearray(width * 4 * 2)
    struct.pack_into("<I", raw32, 4, 0x00123456)
    rows32 = rgb_rows(bytes(raw32), width, 2, width * 4, 32, ((16, 8), (8, 8), (0, 8)))
    assert rows32[0][3:6] == b"\x12\x34\x56", "32 bpp decode"

    documented = [str(n) for n in range(10)] + [chr(c) for c in range(ord("a"), ord("z") + 1)] + [
        "esc", "enter", "tab", "space", "backspace", "up", "down", "left", "right", "help",
        "screenshot"]
    for name in documented + list(KEY_ALIASES):
        assert resolve_key(name) is not None, "key %s" % name
    assert resolve_key("ESC") == 1 and resolve_key("Fn+H") == 138 and resolve_key("fn+j") == 99
    assert resolve_key("fn+x") == 108 and resolve_key("ctrl+c") is None and resolve_key("99") is None

    # Capability bitmap as printed by the device's keyboard.
    sample = "20 8000000000000000 0 300000000 3800000400 effc87fefffff fbbfffffffffcffe"
    caps = parse_key_capabilities(sample)
    for name in documented:
        assert resolve_key(name) in caps, "capability %s" % name
    assert 0 not in caps and 12 not in caps and 13 not in caps
    assert parse_key_capabilities("") == set()
    assert parse_key_capabilities("1 0") == {64}

    # "run --theme T" hands the name to LokiCTL unchecked, after the program
    # path, where systemd-run no longer reads options of its own.
    parser = build_parser()
    assert parser.parse_args(["run"]).theme is None, "run without --theme"
    assert parser.parse_args(["run", "--theme", "constellation"]).theme == "constellation"
    assert parser.parse_args(["run", "--theme=nope"]).theme == "nope", "names are not checked"
    stats = "/run/user/1000/" + FRAME_STATS_NAME
    for launcher in (True, False):
        plain = dev_unit_command(stats, launcher)
        assert plain[0] == "systemd-run" and plain[-1] == BINARY, plain
        assert "--theme" not in plain, plain
        themed = dev_unit_command(stats, launcher, "constellation")
        assert themed[:-2] == plain, themed
        assert themed[-3:] == [BINARY, "--theme", "constellation"], themed
        assert themed.count(BINARY) == 1, themed
    assert ("--working-directory=" + LAUNCHER_DIR) in dev_unit_command(stats, True)
    assert ("--working-directory=" + LAUNCHER_DIR) not in dev_unit_command(stats, False)

    # "snapshot --switch-to T[,T...]" hands the list to LokiCTL unchecked as
    # well, after --theme, the way --theme is handed on.
    shot = "/run/user/1000/snapshot.png"
    plain = parser.parse_args(["snapshot"])
    assert plain.switch_to is None, "snapshot without --switch-to"
    assert snapshot_command(shot, plain) == [BINARY, "--snapshot", shot], snapshot_command(shot, plain)
    switched = parser.parse_args(["snapshot", "--page", "dev-theme", "--theme", "instrument",
                                  "--switch-to", "terminal,constellation", "--settle-ms", "0"])
    assert switched.switch_to == "terminal,constellation", switched.switch_to
    assert parser.parse_args(["snapshot", "--switch-to=nope,,"]).switch_to == "nope,,", "lists are not checked"
    assert snapshot_command(shot, switched) == [
        BINARY, "--snapshot", shot, "--page", "dev-theme", "--theme", "instrument",
        "--switch-to", "terminal,constellation", "--settle-ms", "0"], snapshot_command(shot, switched)

    # "snapshot --keys K[,K...]" is handed on unchecked too, after --theme and
    # before --switch-to. LokiCTL presses the keys inside the snapshot run.
    assert plain.keys is None, "snapshot without --keys"
    assert switched.keys is None, "--keys is only there when given"
    assert "--keys" not in snapshot_command(shot, switched), snapshot_command(shot, switched)
    keyed = parser.parse_args(["snapshot", "--page", "demo-counter", "--keys", "8,x"])
    assert keyed.keys == "8,x", keyed.keys
    assert snapshot_command(shot, keyed) == [
        BINARY, "--snapshot", shot, "--page", "demo-counter", "--keys", "8,x"], snapshot_command(shot, keyed)
    assert parser.parse_args(["snapshot", "--keys=nope,,esc:short"]).keys == "nope,,esc:short", \
        "key names are not checked"
    everything = parser.parse_args(["snapshot", "--settle-ms", "0", "--switch-to", "terminal",
                                    "--keys", "8,esc:long", "--theme", "instrument",
                                    "--page", "demo-counter"])
    assert snapshot_command(shot, everything) == [
        BINARY, "--snapshot", shot, "--page", "demo-counter", "--theme", "instrument",
        "--keys", "8,esc:long", "--switch-to", "terminal", "--settle-ms", "0"], \
        snapshot_command(shot, everything)

    def refused(argv):
        # argparse reports a refused value with SystemExit(2) and a message
        # on stderr, which is kept out of the selftest's output.
        try:
            with contextlib.redirect_stderr(io.StringIO()):
                parser.parse_args(argv)
        except SystemExit as exit_request:
            return exit_request.code == 2
        return False

    # "run --data D" hands the source name to LokiCTL unchecked, after the
    # program path and after any --theme.
    assert parser.parse_args(["run"]).data is None, "run without --data"
    assert parser.parse_args(["run", "--data", "demo"]).data == "demo"
    assert parser.parse_args(["run", "--data=nope"]).data == "nope", "source names are not checked"
    for launcher in (True, False):
        plain = dev_unit_command(stats, launcher)
        assert "--data" not in plain, plain
        assert dev_unit_command(stats, launcher, None, None) == plain
        sourced = dev_unit_command(stats, launcher, data="demo")
        assert sourced[:-2] == plain, sourced
        assert sourced[-3:] == [BINARY, "--data", "demo"], sourced
        both = dev_unit_command(stats, launcher, "terminal", "live")
        assert both[:-4] == plain, both
        assert both[-5:] == [BINARY, "--theme", "terminal", "--data", "live"], both
        assert both.count(BINARY) == 1, both
    args = parser.parse_args(["run", "--data", "sample", "--theme", "instrument"])
    assert (args.theme, args.data) == ("instrument", "sample"), args

    # "snapshot --data D --data-steps N" hands both on after --theme and
    # before --keys; --data-steps must be 0 to 100000, and 0 is kept.
    unsourced = parser.parse_args(["snapshot", "--page", "home", "--keys", "c"])
    assert unsourced.data is None and unsourced.data_steps is None, unsourced
    assert snapshot_command(shot, unsourced) == [
        BINARY, "--snapshot", shot, "--page", "home", "--keys", "c"], snapshot_command(shot, unsourced)
    stepped = parser.parse_args(["snapshot", "--settle-ms", "0", "--switch-to", "terminal",
                                 "--keys", "6,data,data,esc", "--data-steps", "181",
                                 "--data", "demo", "--theme", "instrument", "--page", "home"])
    assert stepped.data == "demo" and stepped.data_steps == 181, stepped
    assert snapshot_command(shot, stepped) == [
        BINARY, "--snapshot", shot, "--page", "home", "--theme", "instrument",
        "--data", "demo", "--data-steps", "181", "--keys", "6,data,data,esc",
        "--switch-to", "terminal", "--settle-ms", "0"], snapshot_command(shot, stepped)
    zero = parser.parse_args(["snapshot", "--data-steps", "0"])
    assert snapshot_command(shot, zero) == [BINARY, "--snapshot", shot, "--data-steps", "0"], \
        snapshot_command(shot, zero)
    sourced = parser.parse_args(["snapshot", "--data=nope"])
    assert snapshot_command(shot, sourced) == [BINARY, "--snapshot", shot, "--data", "nope"], \
        "source names are not checked"
    assert parser.parse_args(["snapshot", "--data-steps", "100000"]).data_steps == 100000
    for value in ("100001", "-1", "x", "1.5"):
        assert refused(["snapshot", "--data-steps", value]), "--data-steps " + value

    # "data [--data D]" runs --print-data, with the source when given.
    plain_data = parser.parse_args(["data"])
    assert plain_data.data is None, "data without --data"
    assert data_command(plain_data) == [BINARY, "--print-data"], data_command(plain_data)
    sourced_data = parser.parse_args(["data", "--data", "sample"])
    assert data_command(sourced_data) == [BINARY, "--print-data", "--data", "sample"], \
        data_command(sourced_data)
    assert data_command(parser.parse_args(["data", "--data=nope"]))[-1] == "nope", \
        "source names are not checked"

    # "stats --seconds N" takes 1 to 60.
    assert parser.parse_args(["stats"]).seconds == 3
    assert parser.parse_args(["stats", "--seconds", "60"]).seconds == 60
    assert parser.parse_args(["stats", "--seconds", "1"]).seconds == 1
    assert refused(["stats", "--seconds", "61"]), "--seconds 61"
    assert refused(["stats", "--seconds", "0"]), "--seconds 0"

    sys.stdout.write("selftest ok\n")
    return 0


# ---------------------------------------------------------------------------
# Command line


def hold_ms_value(text):
    try:
        value = int(text)
    except ValueError:
        raise argparse.ArgumentTypeError("must be a whole number of milliseconds")
    if not 20 <= value <= 2500:
        raise argparse.ArgumentTypeError("must be between 20 and 2500")
    return value


def range_value(low, high, what):
    def convert(text):
        try:
            value = int(text)
        except ValueError:
            raise argparse.ArgumentTypeError("must be a whole number")
        if not low <= value <= high:
            raise argparse.ArgumentTypeError("%s must be between %d and %d" % (what, low, high))
        return value
    return convert


def build_parser():
    parser = argparse.ArgumentParser(prog=PROG, description="Device side of scripts/device.sh.")
    commands = parser.add_subparsers(dest="command", metavar="<command>")
    commands.required = True

    install = commands.add_parser("install", help="install an uploaded package")
    install.add_argument("package")
    install.add_argument("--with-deps", action="store_true")
    install.add_argument("--sha256", default="")
    install.add_argument("--label", default="", help="package file name to show in messages")
    install.set_defaults(handler=cmd_install)

    commands.add_parser("remove", help="stop and remove LokiCTL").set_defaults(handler=cmd_remove)
    run_parser = commands.add_parser("run", help="start LokiCTL on the screen")
    run_parser.add_argument("--theme", help="theme for this run only (passed on to LokiCTL)")
    run_parser.add_argument("--data", help="data source for this run only (passed on to LokiCTL)")
    run_parser.set_defaults(handler=cmd_run)
    commands.add_parser("stop", help="stop LokiCTL, bring back the launcher").set_defaults(handler=cmd_stop)
    commands.add_parser("status", help="show what is running").set_defaults(handler=cmd_status)

    screen = commands.add_parser("screen", help="write the screen as PNG to stdout")
    screen.add_argument("--no-wait", action="store_true")
    screen.set_defaults(handler=cmd_screen)

    key = commands.add_parser("key", help="press keys")
    key.add_argument("keys", nargs="+", metavar="name")
    hold = key.add_mutually_exclusive_group()
    hold.add_argument("--long", action="store_true")
    hold.add_argument("--hold-ms", type=hold_ms_value)
    key.add_argument("--launcher", action="store_true")
    key.set_defaults(handler=cmd_key)

    stats = commands.add_parser("stats", help="memory, CPU and frame time")
    stats.add_argument("--seconds", type=range_value(1, 60, "--seconds"), default=3)
    stats.set_defaults(handler=cmd_stats)

    snapshot = commands.add_parser("snapshot", help="lokictl --snapshot, PNG to stdout")
    snapshot.add_argument("--page")
    snapshot.add_argument("--theme")
    snapshot.add_argument("--data", help="data source (passed on to LokiCTL; default: the sample)")
    snapshot.add_argument("--data-steps", type=range_value(0, 100000, "--data-steps"),
                          help="further readings before the keys")
    snapshot.add_argument("--keys", help="keys to press before the picture (passed on to LokiCTL)")
    snapshot.add_argument("--switch-to", help="themes to switch to live (passed on to LokiCTL)")
    snapshot.add_argument("--settle-ms", type=range_value(0, 60000, "--settle-ms"))
    snapshot.set_defaults(handler=cmd_snapshot)

    data = commands.add_parser("data", help="lokictl --print-data, name=value lines to stdout")
    data.add_argument("--data", help="data source (passed on to LokiCTL; default: live)")
    data.set_defaults(handler=cmd_data)

    logs = commands.add_parser("logs", help="log of the latest 'run' only")
    logs.add_argument("-n", dest="lines", type=range_value(1, 10000, "-n"), default=50)
    logs.set_defaults(handler=cmd_logs)

    commands.add_parser("selftest", help="offline checks").set_defaults(handler=cmd_selftest)
    return parser


def main(argv):
    args = build_parser().parse_args(argv)
    if args.command != "selftest":
        os.environ.setdefault("XDG_RUNTIME_DIR", "/run/user/%d" % os.getuid())
    try:
        return args.handler(args)
    except Failure as failure:
        say(str(failure))
        return 1
    except KeyboardInterrupt:
        say("Interrupted.")
        return 1
    except BrokenPipeError:
        return 1
    except Exception as error:  # never let Python exit with an odd code
        say("%s %s failed: %s: %s" % (PROG, args.command, type(error).__name__, error))
        return 1


if __name__ == "__main__":
    try:
        code = main(sys.argv[1:])
    except SystemExit as exit_request:
        code = exit_request.code if isinstance(exit_request.code, int) else 2
    sys.exit(code if 0 <= code <= 2 else 1)
