#!/usr/bin/env python3
"""Generate the firmware's current/future country-local catalog from pinned tzdb.

All inputs are explicit local archives. This program never accesses the network
or the host zoneinfo database. Regeneration requires Python 3.11+, make and a C
compiler; routine tests use the checked-in catalog and TZif-derived oracle.
"""

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tarfile
import tempfile
from zoneinfo import ZoneInfo

VERSION = "2026c"
ARCHIVES = {
    "tzdata": "e4a178a4477f3d0ea77cc31828ff72aa38feff8d61aa13e7e99e142e9d902be4",
    "tzcode": "b1cffc3ace4c4c7cd0efba2f7add86ec3d0b79da48bcf03582671fd3c8feace8",
}
DATA_FILES = ("africa", "antarctica", "asia", "australasia", "europe", "northamerica",
              "southamerica", "etcetera", "backward")
COUNTRIES = ("US", "CA", "MX")
VALID_FROM = datetime(2026, 9, 1, tzinfo=timezone.utc)
ORACLE_UNTIL = datetime(2038, 1, 1, tzinfo=timezone.utc)


def verify_archive(path, kind):
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    if digest != ARCHIVES[kind]:
        raise ValueError(f"{kind} SHA-256 mismatch: expected {ARCHIVES[kind]}, got {digest}")
    with tarfile.open(path) as archive:
        if archive.extractfile("version").read().strip() != VERSION.encode("ascii"):
            raise ValueError(f"{kind} version must be {VERSION}")


def extract_regular_files(path, destination):
    # Verified release archives contain regular files only; never extract links
    # or let an archive member choose a path outside the temporary source tree.
    with tarfile.open(path) as archive:
        for member in archive.getmembers():
            relative = Path(member.name)
            if not member.isfile() or relative.is_absolute() or ".." in relative.parts:
                raise ValueError(f"unsupported archive member: {member.name}")
            target = destination / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(archive.extractfile(member).read())


def country_zones(source):
    result = []
    for line in (source / "zone.tab").read_text(encoding="ascii").splitlines():
        if not line or line.startswith("#"):
            continue
        country, _, identifier, *_ = line.split("\t")
        if country in COUNTRIES:
            label = identifier.split("/", 1)[1].replace("_", " ")
            result.append((identifier, country, label))
    # Keep country-local Links from zone.tab, even when zic resolves their rules
    # to a canonical zone in another country (e.g. Atikokan -> Panama).
    result.sort(key=lambda row: (COUNTRIES.index(row[1]), row[2]))
    return [("UTC", "UTC", "UTC")] + result


def tzif_parts(data):
    """Read the 64-bit transition times and the POSIX tail, per RFC 9636."""
    if data[:4] != b"TZif" or data[4:5] not in (b"2", b"3", b"4"):
        raise ValueError("expected a TZif v2+ file")

    def header(offset):
        if data[offset:offset + 4] != b"TZif":
            raise ValueError("invalid second TZif header")
        return struct.unpack_from(">6I", data, offset + 20)

    def block_size(counts, width):
        utc, standard, leaps, times, types, chars = counts
        return times * (width + 1) + types * 6 + chars + leaps * (width + 4) + standard + utc

    second = 44 + block_size(header(0), 4)
    counts = header(second)
    time_count = counts[3]
    start = second + 44
    transitions = struct.unpack_from(f">{time_count}q", data, start)
    tail = data[start + block_size(counts, 8):]
    if not tail.startswith(b"\n") or not tail.endswith(b"\n") or tail.count(b"\n") != 2:
        raise ValueError("missing or malformed POSIX TZif footer")
    posix = tail[1:-1].decode("ascii")
    if not posix:
        raise ValueError("zone has no current/future POSIX rule")
    return transitions, posix


def build_outputs(tzdata, tzcode):
    with tempfile.TemporaryDirectory(prefix="cyd-timezones-") as directory:
        temporary = Path(directory)
        source = temporary / "source"
        source.mkdir()
        extract_regular_files(tzdata, source)
        extract_regular_files(tzcode, source)
        subprocess.run(["make", "-s", "zic"], cwd=source, check=True, capture_output=True, text=True)
        zoneinfo = temporary / "zoneinfo"
        subprocess.run([str(source / "zic"), "-b", "fat", "-d", str(zoneinfo), *DATA_FILES],
                       cwd=source, check=True, capture_output=True, text=True)
        zones = country_zones(source)
        catalog = [f"// Generated from IANA tzdb {VERSION}; do not edit by hand.",
                   "// See tools/timezones/README.md for provenance and offline regeneration.",
                   "// Current/future POSIX rules only; historical wall time is not supported.",
                   "static const TimeZone kTimeZones[] = {"]
        oracle = [f"# IANA tzdb {VERSION}; offsets from compiled TZif via Python ZoneInfo.from_file.",
                  f"# UTC sample range: {VALID_FROM.isoformat()} through {ORACLE_UNTIL.isoformat()} (exclusive).",
                  "# Quarterly samples plus one second before/at every stored transition in range.",
                  "# zone_id\tutc_epoch\tutc_offset_seconds"]
        quarterly = {int(VALID_FROM.timestamp())}
        for year in range(VALID_FROM.year, ORACLE_UNTIL.year):
            for month in (1, 4, 7, 10):
                value = datetime(year, month, 15, 12, tzinfo=timezone.utc)
                if VALID_FROM <= value < ORACLE_UNTIL:
                    quarterly.add(int(value.timestamp()))
        sample_count = 0
        for identifier, country, label in zones:
            path = zoneinfo / identifier
            transitions, posix = tzif_parts(path.read_bytes())
            catalog.append("  {" + ", ".join(json.dumps(part) for part in
                                               (identifier, country, label, posix)) + "},")
            samples = set(quarterly)
            for transition in transitions:
                for value in (transition - 1, transition):
                    if VALID_FROM.timestamp() <= value < ORACLE_UNTIL.timestamp():
                        samples.add(value)
            with path.open("rb") as stream:
                zone = ZoneInfo.from_file(stream, key=identifier)
            for value in sorted(samples):
                local = datetime.fromtimestamp(value, timezone.utc).astimezone(zone)
                oracle.append(f"{identifier}\t{value}\t{int(local.utcoffset().total_seconds())}")
                sample_count += 1
        catalog.append("};")
        provenance = {
            "tzdb_version": VERSION,
            "sources": [{"filename": f"{kind}{VERSION}.tar.gz", "sha256": digest,
                         "url": f"https://data.iana.org/time-zones/releases/{kind}{VERSION}.tar.gz"}
                        for kind, digest in ARCHIVES.items()],
            "selection": "All US, CA and MX rows of zone.tab, retaining Link names; plus UTC",
            "countries": list(COUNTRIES),
            "catalog_entries": len(zones),
            "rule_source": "POSIX footers produced by pinned tzcode zic -b fat from pinned tzdata",
            "wall_time_scope": "Current/future offset rules only; no historical wall-time conversion",
            "valid_from_utc": VALID_FROM.isoformat(),
            "oracle_until_utc_exclusive": ORACLE_UNTIL.isoformat(),
            "oracle_source": "Python ZoneInfo.from_file reading compiled TZif, independent of POSIX footer evaluation",
            "oracle_samples": sample_count,
        }
        return {
            "VictronCYD_Modbus/TimeZoneCatalog.inc": "\n".join(catalog) + "\n",
            "tests/host/fixtures/timezone_oracle.tsv": "\n".join(oracle) + "\n",
            "tools/timezones/provenance.json": json.dumps(provenance, indent=2) + "\n",
        }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tzdata", required=True, type=Path, help="explicit local pinned tzdata archive")
    parser.add_argument("--tzcode", required=True, type=Path, help="explicit local pinned tzcode archive")
    parser.add_argument("--output-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--check", action="store_true", help="regenerate offline and fail if checked-in outputs differ")
    args = parser.parse_args()
    try:
        verify_archive(args.tzdata, "tzdata")
        verify_archive(args.tzcode, "tzcode")
        outputs = build_outputs(args.tzdata, args.tzcode)
        if args.check:
            for relative, text in outputs.items():
                path = args.output_root / relative
                if not path.is_file() or path.read_text(encoding="utf-8") != text:
                    raise ValueError(f"generated output differs: {relative}")
        else:
            for relative, text in outputs.items():
                path = args.output_root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text, encoding="utf-8", newline="\n")
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        detail = getattr(error, "stderr", "") or ""
        parser.exit(1, f"{error}\n{detail}")
    print(f"tzdb {VERSION}: {'verified' if args.check else 'generated'} {len(outputs)} offline artifacts")


if __name__ == "__main__":
    main()
