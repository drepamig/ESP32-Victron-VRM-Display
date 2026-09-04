# Issue #1: Configurable time display

Approved in chat on 2026-09-04. Implement directly on `develop`.

Implemented and locally verified. See [verification evidence](../../research/2026-09-04-time-settings.md);
physical acceptance remains pending.

## Behavior

Add a dashboard gear and Settings menu with Time, Wi-Fi, and Back. Preserve
the WAN long-press shortcut. Explicit Wi-Fi Back returns to its entry menu;
automatic exits return to the dashboard. Connection attempts and portals keep
exclusive interaction ownership, including attempts showing the dashboard.

Time settings offer 12/24-hour format and named US, Canadian, Mexican, and UTC
zones. The picker groups by country and pages city/region names. Default to
America/Chicago and 12-hour AM/PM. Drafts apply only on successful Save; Back
discards, picker Back moves one level, and 60 seconds of inactivity discards
and returns to the dashboard. Save failures retain the draft for retry.

Keep the dashboard clock clear of the bounded site name, gear, heartbeat, GX,
and WAN; preserve existing fonts and event-driven rendering. Suppress touch
carryover between screens. Unsynchronized time shows `--:--`.

## Components

1. `TimeSettings` model/store/formatter and generated timezone catalog. Persist
   a versioned stable zone ID plus format in one dedicated NVS record; validate
   loads, avoid unchanged writes, preserve profile/calibration namespaces.
   Pin IANA tzdb 2026c with reproducible generation and offline oracle fixtures.
   Configure existing NTP servers at boot and apply later TZ changes locally.
   System/profile timestamps remain UTC; catalog supports current/future display.
2. Separate Settings UI and production navigation/header policy. Keep Wi-Fi
   lifecycle orchestration in its current controller. Distinguish explicit Back
   from inactivity/expiry so navigation origins are respected.
3. Simulator clock uses UTC fixtures through the production formatter. Register
   sources/tests and add local time-settings navigation/persistence/DST captures.

## Verification and completion

Begin with failing host regressions for persistence/faults, clock boundaries,
regional DST exceptions, draft navigation, inactivity/wraparound, ownership,
held-touch guards, and header geometry. Run the complete host/tooling matrix,
production smoke build, both simulator variants, and all six existing supported
local scenarios plus the new time-settings scenario. Inspect actual screenshots
before promoting recorded goldens. Consume no Wokwi minutes.

Update the tracked status and handoff with evidence. Physical acceptance remains
pending: NTP local time, touch navigation, persistence after reboot, preserved
profiles/calibration, using an NVS-preserving upload. No flashing, pushing,
issue closure, VRM changes, manual date/time, location detection, or runtime
timezone database downloads are included.
