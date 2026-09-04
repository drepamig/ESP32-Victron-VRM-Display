# Pinned timezone data

The firmware catalog is generated from the public-domain IANA Time Zone Database
**2026c**, released 2026-07-08. The source archives are checked in so generation
and tests do not depend on the developer's OS timezone database or network.

| Archive | Official source | SHA-256 |
| --- | --- | --- |
| `tzdata2026c.tar.gz` | [IANA tzdata 2026c](https://data.iana.org/time-zones/releases/tzdata2026c.tar.gz) | `e4a178a4477f3d0ea77cc31828ff72aa38feff8d61aa13e7e99e142e9d902be4` |
| `tzcode2026c.tar.gz` | [IANA tzcode 2026c](https://data.iana.org/time-zones/releases/tzcode2026c.tar.gz) | `b1cffc3ace4c4c7cd0efba2f7add86ec3d0b79da48bcf03582671fd3c8feace8` |

`LICENSE` is copied verbatim from the release. `provenance.json` records the
machine-readable selection, source digests, and oracle coverage. The generator
rejects archives that do not match the pinned hashes before creating outputs.

## Catalog selection and time scope

The 65 choices are UTC plus all 29 US, 23 Canadian, and 12 Mexican rows in
`zone.tab`. This table intentionally supplies country-local names, including
Link entries that would disappear if only canonical `zone1970.tab` rows were
selected. For example, `America/Atikokan` is retained as a Canadian choice even
though its source rules link to Panama. Obsolete backward aliases that are not
country-local `zone.tab` choices, such as `US/Central`, are not added to the menu.
Persistent settings store the selected stable IANA ID, never a list index or an
offset. The UTC entry uses ID and country `UTC`.

Within each country the menu labels are the location portion of the IANA name,
with underscores changed to spaces. The catalog order is UTC, US, CA, MX. The
generated labels preserve nested locations such as `Indiana/Indianapolis` so
regional choices remain distinguishable.

Pinned `tzcode` builds `zic`; its TZif POSIX footers supply the firmware's
current/future offset rules. There is no hand-maintained US-style DST mapping.
Notable 2026c behavior includes permanent UTC-6 for Edmonton/Alberta, permanent
UTC-7 for Vancouver, seasonal DST for Inuvik, and the Mexican border exceptions.
See the release's [NEWS](https://data.iana.org/time-zones/tzdb-2026c/NEWS) and
[northamerica](https://data.iana.org/time-zones/tzdb-2026c/northamerica) source.

The firmware uses proleptic current/future POSIX rules, **not historical civil
time conversion**. The catalog is checked for UTC instants beginning
2026-09-01. Previous civil-time offsets, DST flags, or abbreviations may differ
from full tzdb history. This does not alter UTC timestamps, NTP, or stored
network profile timestamps. Future law changes require a reviewed source pin
update and regeneration; no device or routine test downloads timezone data.

## Offline regeneration

Run with Python 3.11+, `make`, and a C compiler on a POSIX host. Both local archive
arguments are mandatory; the script has no download mode and never uses the
host's zoneinfo directory.

```sh
python3 tools/generate_timezones.py \
  --tzdata tools/timezones/tzdata2026c.tar.gz \
  --tzcode tools/timezones/tzcode2026c.tar.gz
```

The installed local bench Docker image supplies all dependencies. From the
repository root in PowerShell:

```powershell
docker run --rm --network none `
  --mount "type=bind,source=$($PWD.Path),target=/workspace" -w /workspace `
  victron-cyd-virtual-bench:2026-09-03 `
  python3 tools/generate_timezones.py `
  --tzdata tools/timezones/tzdata2026c.tar.gz `
  --tzcode tools/timezones/tzcode2026c.tar.gz --check
```

Remove `--check` to update the three generated outputs:

- `VictronCYD_Modbus/TimeZoneCatalog.inc`
- `tests/host/fixtures/timezone_oracle.tsv`
- `tools/timezones/provenance.json`

`--check` rebuilds in a temporary directory and compares outputs without writing
them. The generator compiles only pinned IANA source, does not execute files
from an unverified archive, and rejects non-regular archive members.

## Verification

The checked-in oracle contains 4,972 UTC instant / offset samples from the
compiled TZif files. Python's `ZoneInfo.from_file` reads those binary files to
produce expected offsets independently of firmware POSIX evaluation. Samples
cover every zone at quarterly dates and one second before/at every recorded
transition from 2026-09-01 through 2037. Abbreviation and DST-flag differences
from IANA's temporary 2026 transition modeling are deliberately outside the
clock's displayed time contract.

`time_settings_test` runs the actual C++ catalog through `setenv`, `tzset`, and
`localtime_r` and checks each expected local date and time. Separate literal
cases cover 2026 fall-back, 2027 spring-forward, half-hour Newfoundland,
midnight/noon, year rollover, and regional fixed-offset exceptions. The test
also covers NVS round trips, malformed or missing records, read/write faults,
namespace isolation, no-op saves, and save/apply rollback. Its `--wrap=setenv`
linker flag injects an application failure without production test hooks.

Routine checks run offline with checked-in artifacts:

```sh
python3 -m unittest discover -s tests/tools -p test_timezones.py -v
bash tools/run-host-tests.sh
```

The Python tests verify archive hashes, all country-local choices, oracle
coverage, mandatory explicit generator inputs, and rejection of changed source
archives. Neither routine test needs `zic`, network access, or Wokwi minutes.

## Settings record

`TimeSettingsStore` uses NVS namespace `timesettings`, key `record`, with a single
versioned string such as `1|12|America/Chicago` or `1|24|UTC`. Missing, malformed,
unknown-version, invalid-format, and unknown-zone records fall back to Chicago
with a 12-hour clock. Loading never writes or repairs NVS. A changed explicit
Save writes zone and format together in one NVS entry; an unchanged effective
value does not rewrite the record.

Loading and saving alone do not alter `TZ`. The UI's Save action uses
`saveAndApplyTimeSettings` to apply the candidate, persist it, and update the
active model only when both operations succeed. A failed persistence write
reapplies the prior active zone. No UI rendering or event dispatch occurs inside
that operation, so draft selections and failed saves do not change the visible
clock.
