#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 LokiCTL contributors
# SPDX-License-Identifier: MIT
#
# Makes assets/data/vulndb.tsv, LokiCTL's built-in list of known security
# holes and outdated versions, which the Ports page checks a service's
# version against. Run it at each release:
#
#   python3 scripts/vulndb/build.py            # downloads what is not cached yet
#   python3 scripts/vulndb/build.py --refresh  # downloads everything again
#
# Sources (see docs/checks.md and docs/third-party/vulnerability-data.md):
#   - NVD's CVE API 2.0: for each product below, every CVE whose CPE
#     applicability names it, with its version ranges and CVSS score. Only
#     product names are sent. No API key: one request every 6.5 s, as the
#     public limit is 5 requests in a rolling 30 s window.
#   - CISA's Known Exploited Vulnerabilities catalog: which CVEs attackers
#     are known to use.
#
# Raw downloads are kept under build/vulndb-cache/ (never committed), and the
# same cache always gives the same file: no clock is read, the list's date
# is the date of the newest NVD answer in the cache, and every row is sorted.
# Python standard library only.

import argparse
import json
import os
import re
import sys
import time
import urllib.parse
import urllib.request

NVD_URL = "https://services.nvd.nist.gov/rest/json/cves/2.0"
KEV_URL = "https://www.cisa.gov/sites/default/files/feeds/known_exploited_vulnerabilities.json"
USER_AGENT = "lokictl-vulndb-build"
PAGE_SIZE = 2000
REQUEST_GAP_S = 6.5
MIN_SCORE = 7.0

# The products: the id the app uses (src/model/service_version.cpp), and the
# CPE names (part:vendor:product) NVD files it under. Some products are filed
# under more than one vendor name over the years.
PRODUCTS = [
    ("apache", ["a:apache:http_server"]),
    ("bind", ["a:isc:bind"]),
    ("boa", ["a:boa:boa"]),
    ("busybox", ["a:busybox:busybox"]),
    ("dnsmasq", ["a:thekelleys:dnsmasq"]),
    ("dropbear", ["a:dropbear_ssh_project:dropbear_ssh", "a:matt_johnston:dropbear_ssh_server"]),
    ("exim", ["a:exim:exim", "a:university_of_cambridge:exim"]),
    ("goahead", ["a:embedthis:goahead"]),
    ("iis", ["a:microsoft:internet_information_services", "a:microsoft:iis"]),
    ("jetty", ["a:eclipse:jetty", "a:mortbay:jetty"]),
    ("lighttpd", ["a:lighttpd:lighttpd"]),
    ("mini_httpd", ["a:acme:mini_httpd"]),
    ("miniupnpd", ["a:miniupnp_project:miniupnpd"]),
    ("nginx", ["a:f5:nginx", "a:nginx:nginx", "a:igor_sysoev:nginx"]),
    ("openssh", ["a:openbsd:openssh"]),
    ("openssl", ["a:openssl:openssl"]),
    ("php", ["a:php:php"]),
    ("proftpd", ["a:proftpd:proftpd", "a:proftpd_project:proftpd"]),
    ("pureftpd", ["a:pureftpd:pure-ftpd"]),
    ("rompager", ["a:allegrosoft:rompager"]),
    ("routeros", ["o:mikrotik:routeros"]),
    ("thttpd", ["a:acme:thttpd"]),
    ("vsftpd", ["a:beasts:vsftpd", "a:vsftpd_project:vsftpd"]),
]

# Product lines that no longer get security fixes, kept by hand. Each row:
# product, the versions it covers, and the page that says so (also listed in
# docs/checks.md). Checked 2026-11-23; look again at each release.
OUTDATED = [
    ("apache", "[,2.4)", "https://endoflife.date/apache-http-server"),
    ("bind", "[,9.20)", "https://kb.isc.org/docs/aa-00896"),
    ("dropbear", "[,2020)", "https://matt.ucc.asn.au/dropbear/CHANGES"),
    ("exim", "[,4.100)", "https://endoflife.date/exim"),
    ("iis", "[,10.0)", "https://endoflife.date/windows-server"),
    ("jetty", "[,12)", "https://endoflife.date/eclipse-jetty"),
    ("nginx", "[,1.30)", "https://endoflife.date/nginx"),
    ("openssh", "[,8.0)", "https://www.openssh.org/releasenotes.html"),
    ("openssl", "[,3.5)", "https://endoflife.date/openssl"),
    ("openssl", "[3.6,4.0)", "https://endoflife.date/openssl"),
    ("php", "[,8.2)", "https://endoflife.date/php"),
    ("proftpd", "[,1.3.8)", "https://endoflife.date/proftpd"),
]

# The acceptance checks of the list: each must be in the output.
MUST_HAVE = [("openssh", "CVE-2024-6387"), ("vsftpd", "CVE-2011-2523")]

VERSION_RE = re.compile(r"^[0-9A-Za-z][0-9A-Za-z._+~-]*$")
CVE_RE = re.compile(r"^CVE-(\d{4})-(\d{4,})$")


def log(text):
    print(text, file=sys.stderr, flush=True)


class Downloader:
    """Fetches URLs, never more often than one request per REQUEST_GAP_S."""

    def __init__(self):
        self.last = None

    def get(self, url, path):
        if self.last is not None:
            wait = REQUEST_GAP_S - (time.monotonic() - self.last)
            if wait > 0:
                time.sleep(wait)
        log(f"  GET {url}")
        request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
        for attempt in range(3):
            try:
                with urllib.request.urlopen(request, timeout=120) as answer:
                    body = answer.read()
                break
            except Exception as error:  # noqa: BLE001 - any failure is retried, then reported
                self.last = time.monotonic()
                if attempt == 2:
                    raise
                log(f"  failed ({error}); trying again in 30 s")
                time.sleep(30)
        self.last = time.monotonic()
        json.loads(body)  # refuse to cache something that is not JSON
        tmp = path + ".part"
        with open(tmp, "wb") as out:
            out.write(body)
        os.replace(tmp, path)


def cached_json(downloader, url, path, refresh):
    if refresh or not os.path.exists(path):
        downloader.get(url, path)
    with open(path, "rb") as source:
        return json.load(source)


def nvd_pages(downloader, cache, cpe, refresh):
    """Every CVE NVD lists for `cpe`, page by page from the cache."""
    name = cpe.replace(":", "_")
    start = 0
    while True:
        query = urllib.parse.urlencode(
            {"virtualMatchString": "cpe:2.3:" + cpe, "resultsPerPage": PAGE_SIZE, "startIndex": start}
        )
        page = cached_json(downloader, f"{NVD_URL}?{query}", os.path.join(cache, f"{name}_{start}.json"), refresh)
        yield page
        start += page.get("resultsPerPage", 0)
        if page.get("resultsPerPage", 0) == 0 or start >= page.get("totalResults", 0):
            return


def walk_matches(nodes):
    for node in nodes:
        for match in node.get("cpeMatch", []):
            yield match
        yield from walk_matches(node.get("nodes", []))


def score_of(cve):
    """The CVSS base score: NVD's own (Primary) first, newest CVSS version
    first; then any other source's, in a fixed order; None without one."""
    metrics = cve.get("metrics", {})
    kinds = ("cvssMetricV31", "cvssMetricV30", "cvssMetricV40", "cvssMetricV2")
    for kind in kinds:
        for entry in metrics.get(kind, []):
            if entry.get("type") == "Primary":
                return float(entry["cvssData"]["baseScore"])
    for kind in kinds:
        entries = sorted(metrics.get(kind, []), key=lambda entry: entry.get("source", ""))
        if entries:
            return float(entries[0]["cvssData"]["baseScore"])
    return None


def range_token(match, version, update, stats):
    """One applicability entry as a token: "=1.2.3", or a range "[a,b)"
    with "[" / "]" inclusive and "(" / ")" exclusive (an empty bound is
    open). None when it cannot be used. An entry for every version of a
    product is left out: it names no version to update to, and would mark
    even the newest release (OpenSSH has two such records)."""
    bounds = {key: match[key] for key in match if key.startswith("version")}
    for value in bounds.values():
        if not VERSION_RE.match(value):
            stats["bad_version"] += 1
            return None
    if bounds:
        low = bounds.get("versionStartIncluding", bounds.get("versionStartExcluding", ""))
        high = bounds.get("versionEndIncluding", bounds.get("versionEndExcluding", ""))
        left = "(" if "versionStartExcluding" in bounds else "["
        right = "]" if "versionEndIncluding" in bounds else ")"
        if version not in ("*", "-"):
            stats["range_with_version"] += 1
        return f"{left}{low},{high}{right}"
    if version == "*":
        stats["all_versions"] += 1
        return None
    if version == "-" or version == "":
        stats["no_version"] += 1
        return None
    exact = version
    if update not in ("*", "-", ""):
        exact += update  # OpenSSH: "8.5" with update "p1" is 8.5p1
    exact = exact.replace("\\", "")
    if not VERSION_RE.match(exact):
        stats["bad_version"] += 1
        return None
    return "=" + exact


def token_key(token):
    return (0 if token.startswith("[") or token.startswith("(") else 1, token)


def cve_key(cve_id):
    found = CVE_RE.match(cve_id)
    return (int(found.group(1)), int(found.group(2))) if found else (0, 0)


def main():
    parser = argparse.ArgumentParser(description="Make assets/data/vulndb.tsv from NVD and CISA KEV.")
    parser.add_argument("--refresh", action="store_true", help="download everything again")
    parser.add_argument("--offline", action="store_true", help="use only the cache; fail if something is missing")
    args = parser.parse_args()

    repo = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    cache = os.path.join(repo, "build", "vulndb-cache")
    nvd_cache = os.path.join(cache, "nvd")
    os.makedirs(nvd_cache, exist_ok=True)
    output = os.path.join(repo, "assets", "data", "vulndb.tsv")

    downloader = Downloader()
    if args.offline:
        def refuse(url, path):
            raise SystemExit(f"--offline: {path} is not cached")
        downloader.get = refuse

    kev = cached_json(downloader, KEV_URL, os.path.join(cache, "kev.json"), args.refresh)
    exploited = {entry["cveID"] for entry in kev.get("vulnerabilities", [])}
    log(f"KEV catalog {kev.get('catalogVersion')}: {len(exploited)} CVEs")

    stats = {"bad_version": 0, "range_with_version": 0, "all_versions": 0, "no_version": 0,
             "rejected": 0, "low_score": 0, "no_rows": 0}
    newest = ""
    rows = {}  # (product, cve) -> [score, kev, tokens]
    for product, cpes in PRODUCTS:
        total = 0
        for cpe in cpes:
            part_vendor_product = cpe.split(":")
            count = 0
            for page in nvd_pages(downloader, nvd_cache, cpe, args.refresh):
                newest = max(newest, page.get("timestamp", ""))
                for item in page.get("vulnerabilities", []):
                    cve = item["cve"]
                    count += 1
                    if cve.get("vulnStatus") == "Rejected":
                        stats["rejected"] += 1
                        continue
                    tokens = set()
                    for configuration in cve.get("configurations", []):
                        for match in walk_matches(configuration.get("nodes", [])):
                            fields = match.get("criteria", "").split(":")
                            if len(fields) < 7 or not match.get("vulnerable"):
                                continue
                            if fields[2:5] != part_vendor_product:
                                continue  # another product of the same CVE
                            token = range_token(match, fields[5], fields[6], stats)
                            if token is not None:
                                tokens.add(token)
                    if not tokens:
                        stats["no_rows"] += 1
                        continue
                    score = score_of(cve)
                    known = cve["id"] in exploited
                    if not known and (score is None or score < MIN_SCORE):
                        stats["low_score"] += 1
                        continue
                    row = rows.setdefault((product, cve["id"]), [score, known, set()])
                    row[2] |= tokens
            log(f"{product:10} {cpe:45} {count} CVEs")
            total += count
        if total == 0:
            raise SystemExit(f"no CVEs at all for {product}: check its CPE names")

    for product, cve_id in MUST_HAVE:
        if (product, cve_id) not in rows:
            raise SystemExit(f"{cve_id} for {product} is missing from the list")
    if not newest:
        raise SystemExit("no NVD answer in the cache")

    lines = [
        "# LokiCTL: known security holes (CVEs) and outdated versions of common network services.",
        "# Made by scripts/vulndb/build.py; do not edit by hand. See docs/checks.md.",
        "# Holes from NVD's CVE API 2.0 (CVSS 7.0 or higher, or known to be exploited), which LokiCTL",
        "# shortened to version ranges; exploited marks from CISA's Known Exploited Vulnerabilities",
        "# catalog (CC0 1.0). This product uses the NVD API but is not endorsed or certified by the NVD.",
        "# Rows: o product range source  (no more security fixes)",
        "#       v product CVE score exploited(K/-) ranges  (\"=x\" one version, \"[a,b)\" a range",
        "#       with [ ] inclusive and ( ) exclusive, an empty bound open)",
        f"date\t{newest[:10]}",
        f"kev\t{kev.get('catalogVersion', '')}",
    ]
    for product, token, source in sorted(OUTDATED):
        lines.append(f"o\t{product}\t{token}\t{source}")
    for (product, cve_id), (score, known, tokens) in sorted(rows.items(), key=lambda item: (item[0][0], cve_key(item[0][1]))):
        score_text = "-" if score is None else f"{score:.1f}"
        lines.append(f"v\t{product}\t{cve_id}\t{score_text}\t{'K' if known else '-'}\t"
                     + " ".join(sorted(tokens, key=token_key)))
    text = "\n".join(lines) + "\n"
    os.makedirs(os.path.dirname(output), exist_ok=True)
    with open(output, "w", encoding="ascii", newline="\n") as out:
        out.write(text)

    per_product = {}
    for (product, _), (_, known, _) in rows.items():
        count, kev_count = per_product.get(product, (0, 0))
        per_product[product] = (count + 1, kev_count + (1 if known else 0))
    for product, (count, kev_count) in sorted(per_product.items()):
        log(f"  {product:10} {count:4} holes kept, {kev_count} exploited")
    log(f"skipped: {stats}")
    log(f"wrote {output}: {len(rows)} holes, {len(OUTDATED)} outdated lines, {len(text)} bytes, date {newest[:10]}")


if __name__ == "__main__":
    main()
