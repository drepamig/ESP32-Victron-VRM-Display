# Resume prompt for the Linux Codex task

Copy the text below into the first message of the Linux task after opening this
repository as its local project.

---

Continue the ESP32 Venus-Starlink touch-bridge project on branch
`codex/esp32-venus-starlink-touch-bridge`.

First read, in order:

1. `docs/handoff/linux-2026-08-31/README.md`
2. `docs/handoff/linux-2026-08-31/VERIFY_LINUX.md`
3. the root `README.md`
4. `docs/README.md`

Treat the handoff as the recovery source of truth. Do not restart Tasks 1-8 or
repeat completed Task 9 calibration/countdown/navigation work. The exact
firmware currently flashed on the bench ESP32 is commit
`a6827e6e92db1870f70ccacd73f8d2b0cf4d5a20`; later commits may contain only
handoff documentation.

Before making changes, verify the branch, working tree, firmware commit in
history, remotes, ignored `VictronCYD_Modbus/secrets.h`, pinned Arduino-ESP32
3.3.11 core, and XPT2046_Touchscreen 1.4 library. Never read a secret value into
chat or commit it. Do not erase NVS or modify Venus.

The earlier physical feedback and top-row touch checkpoints are complete.
Physical validation of the newer on-device password keyboard is still pending.
Resume with controlled upstream provisioning: keep the bench upstream
available, select it from `Nearby`, validate the masked keyboard and Connect
flow first, then validate **Use phone** as the time-limited private portal
fallback. Ask the user for the current upstream SSID only when provisioning
begins; do not add it to tracked files or logs.

Continue the remaining Task 9 physical keyboard, provisioning, NAPT,
bad-credential correction and rollback, second-profile selection/deletion,
phone-fallback portal-boundary, five-minute loss, and reconnect scenarios
exactly as listed in the handoff.
Record only observed hardware behavior and pause when user interaction is
required. Real Starlink and live Venus/GX validation remain deferred until the
field hardware is co-located.

Use strict test-first development for any new defect found, obtain review, run
the full host matrix and Arduino compile, then flash only the exact reviewed
image while preserving NVS.

---
