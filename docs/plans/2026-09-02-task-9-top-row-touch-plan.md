# Task 9: Correct calibrated top-row touch mapping

## Context

The three post-flash feedback checks passed on physical hardware. A new
observation showed that `Back` required several taps, while a deliberate tap
on its lower edge worked reliably.

The calibration UI measures raw coordinates at targets inset 20 pixels from
each display edge. `TouchCalibration` stores those measurements as its extrema,
but `mapTouchPoint()` currently maps them to `(0, 0)` and
`(width - 1, height - 1)`. A physical touch near the center of the top-row
buttons can therefore map above their `y = 4` hitboxes. The lower-edge probe
confirms this coordinate mismatch; it does not implicate the 40 ms contact
debounce.

## Global constraints

- Fix the calibration-to-display coordinate contract at its source. Do not
  enlarge only the `Back` hitbox and do not change touch debounce timing.
- Preserve the existing saved calibration format and NVS contents. The fix
  must work with the calibration already stored on the board.
- Use one shared named 20-pixel calibration-target inset for both target
  drawing and coordinate mapping so they cannot drift apart.
- Preserve swapped-axis and inverted-axis behavior.
- Preserve valid in-bounds output for raw samples outside the calibrated
  range and for positive display dimensions too small to use the normal
  inset.
- Do not modify network, credential, portal, Modbus, or Venus behavior.
- Do not touch or commit the unrelated `docs/handoff.lnk` file.
- Follow strict TDD: write the focused regression first, run it and capture
  the expected assertion failure, then make the smallest production change
  and rerun the focused and complete host suites.

## Required changes

1. In `tests/host/touch_mapping_test.cpp`, add or revise literal assertions
   proving that for a 320x240 display:
   - the raw top-left calibration sample maps to `(20, 20)`;
   - the raw bottom-right sample maps to `(299, 219)`;
   - swapped axes preserve those inset endpoints;
   - inverted axes reverse the inset endpoints; and
   - out-of-range raw input remains within display bounds.
2. Before editing production code, compile and run that test and record that
   it fails because the current implementation returns edge coordinates.
3. In `VictronCYD_Modbus/TouchMapping.h`, introduce the shared named inset and
   map calibrated raw extrema to the corresponding inset display endpoints.
   For a dimension too small for two 20-pixel insets, fall back to that
   dimension's full valid range.
4. In `VictronCYD_Modbus/TouchInput.cpp`, use the shared inset when positioning
   all four calibration targets; retain the existing opposite-edge expression
   of `dimension - 1 - inset`.
5. Rerun the focused test, all nine host suites with warnings treated as
   errors, `git diff --check`, and a full Arduino-ESP32 3.3.11 compile.
6. Commit the narrowly scoped tested fix. Do not upload it; physical upload
   and validation happen only after review.

## Physical acceptance after review

After the reviewed image is flashed without erasing NVS, a single deliberate
tap near the center of `Back`, `Saved`, and `Nearby` must register. The three
previously passed feedback checks must remain passing.

Observed on 2026-09-02: passed. Calibration persisted across the verified
upload, all three center taps registered, and the earlier feedback checks
remained passing.
