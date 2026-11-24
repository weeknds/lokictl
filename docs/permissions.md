<!--
SPDX-FileCopyrightText: 2026 LokiCTL contributors
SPDX-License-Identifier: MIT
-->

# Permissions and what LokiCTL changes

LokiCTL runs as the normal user that the launcher runs as. Nothing in it runs
as root and nothing is setuid. A few features need rights a normal app does
not have, so the package adds three small things: a polkit rule and two
helper programs with one Linux capability each. This page says what each
one allows, and what LokiCTL writes on the device.

The LAN speed test runs Debian's `iperf3` program as your user, only while
you use it, and needs no extra rights. It doesn't need iperf3's own server
(`iperf3.service`): if apt asks whether to start it, keep the default, No.

The [README](../README.md#permissions) has the short version.

## The polkit rule

The package installs a polkit rule,
`/usr/share/polkit-1/rules.d/60-lokictl.rules` (source:
`config/polkit/60-lokictl.rules`), because apps started by the launcher are
not an active login session and may not change NetworkManager's settings on
their own. It lets members of the `netdev` group do exactly three
NetworkManager actions:

- control the network (`network-control`);
- change system-wide connection profiles (`settings.modify.system`);
- share the network through a protected (WPA) hotspot
  (`wifi.share.protected`). An open hotspot is not allowed.

The modes (Router, Share, VPN) need these. Router also needs
`dnsmasq-base` and `nftables`, which the package depends on. Removing the
package removes the rule.

**What that costs.** polkit cannot tell programs apart, so **every program
that a `netdev` member runs gets these three rights, not only LokiCTL**. On
the Cardputer that is the user the launcher runs as, so any program
started as that user, from the launcher, a terminal or SSH, may turn the
Wi-Fi off, switch networks, change or delete saved Wi-Fi profiles
(passwords included), and start a WPA hotspot, without being asked. It
cannot install software, read other users' files or open a hotspot
without a password. A narrower rule would need a group of its own for
LokiCTL, and the launcher's session would have to log in again before it
counted, so the rule stays as it is. If no other program you run should
have these rights, keep untrusted programs off the Cardputer, or remove
the package when you don't use the modes.

## lokictl-helper (CAP_NET_ADMIN)

Blocking a device, Share's second Wi-Fi interface, the VPN gateway's
kill switch and the trap's low ports need one right the app itself does not
have: network administration. The package installs a small helper program for that,
`/usr/libexec/lokictl/lokictl-helper` (source: `src/helper/lokictl_helper.c`).
It is not setuid; the package's install script gives it the single Linux
capability `CAP_NET_ADMIN` (with `setcap`, from `libcap2-bin`) and lets only
root and the `netdev` group run it (owner `root:netdev`, mode 0750).

It does one of twelve fixed commands:

| Command | What it does |
|---|---|
| `block router\|share MAC...` | blocks these devices on that hotspot (Router's `wlan0` or Share's `lokiap0`), only while it runs |
| `unblock router\|share MAC...` | lets them through again |
| `list` | prints the kill switch state, whether the trap is set, and the blocked devices |
| `clear` | removes LokiCTL's firewall table |
| `ap-add` | adds Share's interface, `lokiap0` |
| `ap-del` | removes it |
| `kill-on` | arms the VPN gateway's kill switch |
| `kill-off` | disarms it |
| `trap-on` | sends the trap's low ports on to the ports LokiCTL listens on, for as long as it runs |
| `trap-off` | removes the trap's chain a killed `trap-on` left behind |
| `survey STICK` | adds a listen-only (monitor mode) interface, `lokimon0`, on a USB Wi-Fi stick and hops its channels until stopped, then removes it |
| `survey-off` | removes `lokimon0` |

It only ever changes its own nftables table, `inet lokictl`: a set of
blocked hardware addresses with two rules that drop what such a device
sends in through the hotspot's interface, and, for the kill switch, two
chains of their own. No other table is touched and the rule set is never
flushed. Each command is one nft transaction, done whole or not at all.
`ap-add` and `ap-del` add and remove `lokiap0` with `iw` (which the
package depends on) and fixed arguments.

`block` and `unblock` take a fixed word for the hotspot, `router` or
`share`, and nothing else names an interface. The helper itself checks
that this interface really runs a hotspot (`iw dev wlan0 info` or `iw dev
lokiap0 info` says `type AP`) and refuses otherwise, so in Client mode,
where `wlan0` is the Cardputer's own way to the internet, no device can be
blocked on it. The two rules then match only that interface.

The kill switch has two chains. One drops what comes in through either
hotspot interface and would leave by any interface other than the tunnel
(`lokiwg0`); the other drops DNS that the hotspot's DNS server would send
outside the tunnel. That DNS server is the `dnsmasq` NetworkManager starts
for the hotspot: `kill-on` reads the user it runs as from
`/proc/<pid>/status`, counting only a `dnsmasq` whose parent is
NetworkManager running as root (a program that only calls itself
`dnsmasq` does not count). The kill switch is armed before the hotspot
starts, when no `dnsmasq` runs yet; then it takes dnsmasq's default user,
`nobody`, and LokiCTL arms it once more as soon as the hotspot is up. If
the DNS server runs as root, or two run as different users, `kill-on`
changes nothing and the page says `DNS server user unclear`: a rule for
root would also stop the Cardputer's own DNS outside the tunnel.

The trap (a honeypot) listens as your user on fixed high ports; only its
low ports need the helper. `trap-on` takes no arguments. It adds a nat
chain of its own, `trap`, that sends new IPv4 connections coming in
through `eth0`, `wlan0` or `lokiap0` to port 21, 23, 80 or 445 of the
Cardputer itself on to port 30021, 30023, 30080 or 30445. Connections the
Cardputer only passes on (a hotspot device browsing the web) are never
touched. A port is sent on only while
nothing on the Cardputer listens on it (read from `/proc/net/tcp` and
`tcp6`), so the real SSH on 22, Remote view's 8780 and any other service
are never taken over, and only while a socket of the user who runs the
helper listens on the high port, so the connections only ever reach
LokiCTL. `trap-on` keeps running while the trap does and checks both
every 3 seconds; a port that fails one is dropped. It removes its chain
when LokiCTL stops the trap, quits or dies (it holds a pipe from LokiCTL
and gets a signal when LokiCTL ends), or when no port is left.
`trap-off` removes a chain that a killed `trap-on` left behind. Neither
ever removes the table or another chain, so a kill switch armed at the
same moment always stays. Ports 3389 and 8080 need no helper; LokiCTL
listens on them itself.

The Wi-Fi survey uses an optional USB Wi-Fi stick, never the built-in
Wi-Fi. `survey` takes the stick's interface name and checks it itself
before it touches anything: a plain name (letters, digits, `.`, `_`, `-`),
never `wlan0` or `lokimon0`, an interface with a `wireless` folder in
`/sys/class/net` whose device is a USB interface. It takes only a stick
that is down and in managed mode (so nothing else is using it), and it
never changes the stick's own interface. It notes the stick's interface
number and its radio (`phy`), and checks that the name still leads to the
same interface after every check. It then runs `iw phy PHY interface add
lokimon0 type monitor` on that radio, checks that `lokimon0` really sits on
it (or removes it again), brings `lokimon0` up, and then runs `iw dev
lokimon0 set channel N` every 250 ms over channels 1 to 14, dropping each
channel the kernel refuses under the country's rules. Monitor mode only
receives; nothing is sent. It removes `lokimon0` (`iw dev lokimon0 del`)
when LokiCTL closes its standard input or dies, on SIGTERM, after 15
minutes at most, or when the stick is pulled out (which takes `lokimon0`
with it). `survey-off` takes no argument and does the same for a
`lokimon0` a killed survey left behind; `survey` also removes such a
leftover before it starts, and removing the package removes one too.
Both remove only a `lokimon0` in monitor mode on a USB stick. These two
commands touch no firewall.

Every `nft` and `iw` the helper runs gets an empty environment and is
killed if it has not finished within 5 seconds, or when the helper itself
ends, so none is ever left running with the helper's right. A stop of
`survey` or `trap-on` cuts a running channel change short, but never the
step that removes what they added. The helper also clears any blocked
signals it was started with, so SIGTERM always reaches it.

LokiCTL removes the table when the hotspot stops, and `lokiap0` when Share
stops, and both when it quits (unless "Keep mode after quitting" is on);
removing the package removes them too (it runs the helper's `ap-del` and
`survey-off`). The trap's chain goes when the trap is taken away and
whenever LokiCTL quits, keep mode or not; one left by a run that was
killed goes at the next start. After a trap, the table itself may stay,
empty, until LokiCTL quits or starts again. Like the polkit rule, **every
program of a `netdev` member may run the helper**, so it could block or
unblock devices on a running hotspot, add or remove `lokiap0`, switch the
kill switch, send the trap's four low ports on to the high ports (only
where nothing listens on them and only to that program's own user) and add
and remove a listen-only `lokimon0` on an idle USB Wi-Fi stick, and
nothing else.

## lokictl-capture (CAP_NET_RAW)

The Discover page, Watch and the Map's switch view need a second right:
hearing the network's raw traffic.
A second, separate helper does that,
`/usr/libexec/lokictl/lokictl-capture` (source: `src/helper/lokictl_capture.c`),
with the single capability `CAP_NET_RAW`, again not setuid and only for
root and the `netdev` group. It does one of five fixed commands,
`discover`, `watch`, `topology`, `names` or `survey`, each with an
interface and a number of seconds (at most 30 minutes), which differ only
in their fixed filter (and `survey` in the kind of interface). It
opens a packet socket with that filter: for `discover` only ARP, DHCP, mDNS
(UDP to port 5353) and SSDP (UDP to port 1900); for `watch` only ARP, DHCP,
TCP connection requests (SYN without ACK) sent to the interface's own
IPv4 address, and IPv6 router announcements (ICMPv6 type 134). The address
is read from the kernel before the filter is set, like for `names`, so a
connection that a device on the Cardputer's hotspot makes to anywhere else
(which reaches the Cardputer only to be passed on) is never heard; on an
interface without an IPv4 address `watch` hears no connection requests at
all. Every IPv4 test checks first that the packet's IPv4 header is whole.
For `topology` only LLDP
(EtherType 0x88cc to `01:80:c2:00:00:0e`) and CDP (to `01:00:0c:cc:cc:cc`,
Cisco's SNAP protocol 0x2000), the frames in which switches and routers say
their name and port; for `names` only IPv4 UDP and TCP to port 53 (DNS
questions) sent to the interface's own IPv4 address, which the helper
reads from the kernel before it sets the filter (the Talk page: the names
the devices on LokiCTL's hotspot look up; answers and lookups sent on to
other servers are not passed); for `survey` only `lokimon0`, the
listen-only interface the firewall helper adds on a USB Wi-Fi stick, in
monitor mode (never `wlan0`, never an Ethernet-type interface), and of
what it hears only beacons, probe answers and kick-off frames
(deauthentication, disassociation) whole, and of every other frame only
the radio header and the frame's kind, never its addresses or content
(the Survey page). Once the socket is bound, the helper asks the kernel
which interface it is bound to and stops unless that is the interface it
checked, of the right kind. It is never in promiscuous mode. `discover`,
`watch` and `names` join no multicast group; `topology` asks the network card to pass those two
link-layer groups to its socket (`PACKET_ADD_MEMBERSHIP`), because many
cards drop them otherwise. That sends nothing, changes no setting, and the
kernel removes it again when the helper ends. Then it drops every capability and
locks itself (seccomp) so that it can only receive, never send. It writes
what it hears to the app as a PCAP stream on a pipe (no file) and stops at
its time limit, after 16 MiB, or when the app goes away; the app reads the
stream without any rights; for Watch the app runs it again every 30
minutes while Watch is on, and for the Talk page while the hotspot runs. **Every program of a `netdev` member may run it
too**, so it could listen to the ARP, DHCP, mDNS and SSDP traffic that
reaches the Cardputer, to connection requests made to the Cardputer's own
address, to
IPv6 router announcements, to switches' LLDP and CDP frames and to the
DNS questions sent to the Cardputer's own addresses, and to the Wi-Fi
beacons and kick-off frames a stick's `lokimon0` hears, and nothing
else.

Both helpers are only needed for the features above. Without them the rest
of the app works, and the pages say what is missing (`Block failed: helper
not installed`, `Capture helper not installed`).

## Saved captures

Key 7 (SAVE) on the Discover page writes the run to a standard PCAP file
that Wireshark or `tcpdump -r` opens: what was heard so far, then the rest
of the run as it comes. The app writes it, not the helper, with the user's
own rights, to `~/.local/share/lokictl/captures/` (or
`$XDG_DATA_HOME/lokictl/captures/`), named after the time it was made, for
example `20261123-075130.pcap`. The folder is private (0700) and so is
each file (0600). A file holds at most one run (5 minutes) and at most
4 MB of it (the page then says `full`). A run is kept in memory until the
next run starts, so it can still be saved after it ended. Nothing is sent
anywhere; copy the file off the Cardputer yourself, for example with
`scp`, and delete it when you no longer need it: it holds what your
network's devices said about themselves.

## Reports

Key 8 (SAVE) on Health, R on Watch and Discover, or the command `report`
write a report: one web page and a table of the devices (CSV), with the
same time in their names, for example `lokictl-20261123-075130.html` and
`lokictl-20261123-075130-devices.csv`. The app writes them with the user's
own rights to `~/Documents/LokiCTL/reports/`; that folder and `LokiCTL`
above it are private (0700), and so is each file (0600). A new report
never writes over an old one. Remote view's page lists them for download
by the paired browser; nothing else sends them anywhere. Delete them when
you no longer need them: they hold what your network's devices said about
themselves.

## WireGuard files

The VPN mode reads WireGuard client files (`.conf`) from
`~/.config/lokictl/wireguard/`. LokiCTL creates the folder at its first
start and makes it and the files in it readable by you only (0700 and
0600). A file added from the web dashboard (Remote view's Modes page, at
most 16 KB) is checked first and saved as a new file of mode 0600; it is
never sent back to the browser. Only a file's name and its server are ever
shown, and neither is logged. The keys stay in the file and in
NetworkManager's in-memory profile (`LokiCTL VPN`, interface `lokiwg0`),
which is deleted when you turn the tunnel off or LokiCTL quits. Lines such
as `PreUp` and `PostUp` are ignored, so nothing in a file is ever run.
When the file names DNS servers and sends all traffic through the tunnel,
only those servers are used while it runs. The file never leaves the
device.

## Files LokiCTL writes

- `~/.config/lokictl/lokictl.conf`, readable by you only (0600, set at
  every save, in a folder that is yours only, 0700): the theme,
  "Dim when idle", "Keep mode after quitting", the hotspot's name and
  password (the password is made once, at the first switch to Router or
  Share), the list of blocked devices (`blocked_devices=`) and Watch's
  known devices (`known_devices=`). The app reads `speed_test_server=`
  there but never writes it.
- `~/.config/lokictl/wireguard/`, the folder for your WireGuard files (see
  above).
- `~/.local/share/lokictl/captures/`, only when you save a capture.
- `~/Documents/LokiCTL/reports/`, only when you save a report.
- `~/Pictures/Screenshots/`, only when you press fn+J.
- `/sys/class/backlight/backlight/brightness`: LokiCTL dims the screen
  while you don't use it and puts the old value back when you press a key
  or quit (see [Battery care](data.md#battery-care)).

## When LokiCTL quits

Whenever LokiCTL quits, the device goes back to Client: the hotspot, the
tunnel, `lokiap0` and the firewall table are removed, and the Wi-Fi network
it was on before comes back (in Share and VPN it never left). To keep the
mode running after LokiCTL quits instead, switch "Keep mode after quitting"
on in Settings (saved as `keep_mode=yes`); the next start then shows the
mode, with blocked devices and the gateway's kill switch, and can switch
back to Client.

The hotspot's and the tunnel's profiles are only ever kept in memory, so a
restart of the device always ends in Client too. If LokiCTL is killed
instead of quitting, the hotspot stays on until LokiCTL starts again; the
Modes page then shows Router or Share and can switch back.

## Remote view

Remote view runs a small web server on port 8780 while you switch it on.
It is off at every start, listens on your local network's private address
only and needs a one-time code from the Cardputer's screen. It uses plain
HTTP, so use it on networks you trust. [remote.md](remote.md#security-model)
explains how it is kept private.

## Store permission flags

The store asks every app to declare seven permissions in `app-builder.json`
(`store.permissions`). Each one says what the shipped app really does, not
what the hardware could do. LokiCTL's:

| Flag | Value | Why |
|---|---|---|
| `network` | true | It is a network tool: it scans, probes, runs a hotspot and a VPN, and sends a few requests to the internet (see the [README](../README.md#data-use)). |
| `additional_hardware` | true | The Wi-Fi survey works only with a USB Wi-Fi stick, and LokiCTL looks for such sticks among the network interfaces. The stick is optional: everything else uses only the built-in Wi-Fi and Ethernet port. The store counts USB accessories under this flag, so it is declared. |
| `background_service` | false | The launcher starts LokiCTL when you open it. The package installs no systemd unit, and nothing of LokiCTL keeps running after it quits. "Keep mode after quitting" leaves NetworkManager's hotspot and VPN profiles running, which NetworkManager itself looks after; Debian's iperf3 server is never switched on. |
| `camera` | false | The QR codes (hotspot, security holes, sign-in page) are only shown on the screen, for your phone to scan. LokiCTL never uses the camera. |
| `microphone` | false | Not used. |
| `imu` | false | Not used. |
| `external_display` | false | LokiCTL draws only on the built-in screen. Remote view shows the screen in a web browser over the network, which is covered by `network`. |
