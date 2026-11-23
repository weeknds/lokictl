<!--
SPDX-FileCopyrightText: 2026 LokiCTL contributors
SPDX-License-Identifier: MIT
-->

# Checks: known security holes and old versions

After a port scan, LokiCTL checks the software each open port named
against a list it carries with it. The list works offline: the app never
looks anything up on the internet for this.

## What you see

On the Ports page each open port can get a short tag. Only the worst one
shows:

| Tag | What it means |
|---|---|
| `Attacked` | The version has a known security hole that attackers are using right now (it is in CISA's list of exploited holes). |
| `3 holes` | The version has known serious security holes (severity 7 or more out of 10). |
| `Old` | That line of the software no longer gets security fixes. |
| `Check` | The port said which system built it (Debian, Ubuntu and so on). Such builds often get fixes without a new version number, so the holes may already be fixed. Install the device's updates and look again. |

Ports with a tag move to the front of the list when not all ports fit.

`7` (or `Enter`) opens the host detail. Under each port it shows the
software in short (`remote login · OpenSSH 10.0`, the whole text it sent on
the next line when it says more and fits), then one line per hole, worst
first (`Hole attackers use`, `Very serious hole`, `Serious hole`,
`Known hole`), each with its official number (the CVE number) next to it.
At most 8 holes are listed per port; `+5 more holes` says how many more
there are. `Old: no more security fixes` follows for an old version. The
last line says which list was used (`Holes list from 2026-11-23`).

`6` (More) or `Enter` opens the findings one at a time: the product and
port, what it is and what to do in a few short sentences, the severity
score (0 to 10, 10 is worst), the official number and a link to read
more, also as a QR code for a phone. `Z`/`C` (or `F`/`X`, `5`, `6`) step
through them; `Enter`, `4` or `ESC` go back. The help panel (`fn+H`)
explains the tags and the keys.

## It is a version check

LokiCTL only compares the version a service *says* with the list. It never
tries a hole, guesses a password or logs in. So:

- A device may have been fixed without its version changing (Debian,
  Ubuntu and router makers do this all the time). That is what `Check` is
  for, but a maker may also patch without saying so.
- A device may lie about its version, or say none. A port that names no
  version gets no finding.
- Only the products below are known. Anything else gets no finding, which
  does not mean it is safe.

## Where versions come from

- The greeting of SSH, FTP, SMTP, POP3, IMAP and Telnet servers
  (`OpenSSH_9.6p1`, `(vsFTPd 2.3.4)`, `ESMTP Exim 4.92`).
- A web server's `Server:` and `X-Powered-By:` header lines, from the one
  `HEAD` request a web port gets (`nginx/1.18.0`, `PHP/7.4.33`).
- On port 53, one DNS question over TCP for the server's version
  (`version.bind`, class CHAOS). dnsmasq answers `dnsmasq-2.90`, BIND
  answers its version number. It counts in Sent like any other request.

The text is read by hand (`src/model/service_version.cpp`), with no
regular expressions: each known name followed by its version, written as
`name/1.2`, `name_1.2`, `name-1.2` or `name 1.2`.

Known products: Apache httpd, BIND, Boa, BusyBox, dnsmasq, Dropbear, Exim,
GoAhead, IIS, Jetty, lighttpd, mini_httpd, MiniUPnPd, nginx, OpenSSH,
OpenSSL, PHP, ProFTPD, Pure-FTPd, RomPager, MikroTik RouterOS, thttpd and
vsftpd. Samba, Synology DSM and QNAP QTS were left out because they do not
say their version in anything LokiCTL reads.

## How versions are compared

Numbers compare as numbers and letters as text, so `1.0.2k` is newer than
`1.0.2` and `4.100` newer than `4.99`. `rc`, `beta`, `alpha`, `pre` and
`dev` come before the release they lead to. A few products need help:
OpenSSH's portable suffix is dropped for ranges (`9.6p1` is 9.6), but kept
for a single version NVD names (`9.3p1` is not `9.3p2`, the release that
fixed CVE-2023-38408; a bare `9.3` covers 9.3 and 9.3p1); Jetty's build
date is dropped (`9.4.44.v20210927` is 9.4.44), and Boa's `0.94.14rc21`
is written `0.94.14.21` as NVD files it.

The ranges are NVD's as they are. Where NVD's end is a little wide, the
check is too: NVD files CVE-2024-6387 as "8.6 up to and including 9.8",
so OpenSSH 9.8p1, the release that fixed it, is still listed with it.

## The list

`assets/data/vulndb.tsv`, about 165 KB, installed as
`/usr/share/lokictl/data/vulndb.tsv`. It is read line by line only while a
check runs, right after a scan ends, and nothing of it stays in memory but
what matched. Its date shows in the host detail and on the Status page.

It is made by `scripts/vulndb/build.py` (Python standard library only) from:

- **NVD's CVE API 2.0**: for each product, every CVE whose applicability
  names it, with the version ranges marked vulnerable for that product and
  the CVSS base score (NVD's own score first). Kept: score 7.0 or more, or
  known to be exploited. Left out: rejected CVEs, and records that mark
  every version of a product, since they name no version to update to.
- **CISA's Known Exploited Vulnerabilities catalog**: which of those holes
  attackers are using.

Data terms and the notice NVD asks for are in
[third-party/vulnerability-data.md](third-party/vulnerability-data.md).

### Rebuilding it

Run this at each release:

```sh
python3 scripts/vulndb/build.py
```

It downloads what is not cached yet into `build/vulndb-cache/` (never
committed), one NVD request every 6.5 s, which takes about four minutes.
`--refresh` downloads everything again, `--offline` uses only the cache.
The same cache always gives the same file byte for byte: the list's date
is the date of the newest NVD answer, and every row is sorted. The script
stops with an error if a product gets no CVEs at all (a wrong CPE name) or
if CVE-2024-6387 (OpenSSH) or CVE-2011-2523 (vsftpd) is missing.

Then run the checks on the Mac:

```sh
cmake --build --preset darwin-arm64-dbg
./build/darwin-arm64/Debug/lokictl_tests
./build/darwin-arm64/Debug/lokictl_tests "OpenSSH_9.6p1 Ubuntu-3ubuntu13.5"
```

The first runs every check (OpenSSH 8.5p1 to 9.7p1 have CVE-2024-6387,
vsftpd 2.3.4 has CVE-2011-2523, a Debian build is "may already be fixed",
an unknown product gives nothing, and more). With a text, it prints what
the list says about it.

### Old versions

A short table kept by hand in `build.py`, checked on 2026-11-23. Look at
each source again at every release.

| Product | Old | Source |
|---|---|---|
| Apache httpd | before 2.4 | https://endoflife.date/apache-http-server |
| BIND | before 9.20 (9.18 ended June 2026) | https://kb.isc.org/docs/aa-00896 |
| Dropbear | before 2020 | https://matt.ucc.asn.au/dropbear/CHANGES (rule of thumb, see below) |
| Exim | before 4.100 | https://endoflife.date/exim |
| IIS | before 10.0 (Windows Server 2012 R2 and older) | https://endoflife.date/windows-server |
| Jetty | before 12 | https://endoflife.date/eclipse-jetty |
| nginx | before 1.30 | https://endoflife.date/nginx |
| OpenSSH | before 8.0 | https://www.openssh.org/releasenotes.html (rule of thumb, see below) |
| OpenSSL | before 3.5, and 3.6 | https://endoflife.date/openssl |
| PHP | before 8.2 | https://endoflife.date/php |
| ProFTPD | before 1.3.8 | https://endoflife.date/proftpd |

OpenSSH and Dropbear have no published end-of-life dates: fixes only go
into new releases. Versions older than 2019 (OpenSSH 8.0) or 2020
(Dropbear 2020.79) are marked old as a rule of thumb.
