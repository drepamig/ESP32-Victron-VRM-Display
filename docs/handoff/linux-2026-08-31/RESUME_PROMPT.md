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
`1d5f95cb10aeee5b76dd7c4b1c9081cf9260231e`; later commits may contain only
handoff documentation.

Before making changes, verify the branch, working tree, firmware commit in
history, remotes, ignored `VictronCYD_Modbus/secrets.h`, pinned Arduino-ESP32
3.3.11 core, and XPT2046_Touchscreen 1.4 library. Never read a secret value into
chat or commit it. Do not erase NVS or modify Venus.

Resume at the physical checkpoint, not implementation: verify that `Nearby`
and `Refresh` each show `Scanning...` immediately, and verify that sliding out
of a WAN hold restores the normal header immediately. Keep the controlled bench
upstream available but do not select it until those three observations pass.
Ask the user for the current upstream SSID only when provisioning begins; do
not add it to tracked files or logs.

After the checkpoint passes, continue the remaining Task 9 provisioning, NAPT,
bad-credential retention, second-profile selection/deletion, portal-boundary,
five-minute loss, and reconnect scenarios exactly as listed in the handoff.
Record only observed hardware behavior and pause when user interaction is
required. Real Starlink and live Venus/GX validation remain deferred until the
field hardware is co-located.

Use strict test-first development for any new defect found, obtain review, run
the full host matrix and Arduino compile, then flash only the exact reviewed
image while preserving NVS.

---
