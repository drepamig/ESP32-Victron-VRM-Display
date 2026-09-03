# On-device Wi-Fi password entry design

Date: 2026-09-02
Design status: Approved in design discussion.

Implementation status, reconciled 2026-09-03: implemented with host, build,
and recorded virtual-bench coverage; physical keyboard validation remains
pending. The requirements below remain the acceptance target. Refer to
[current status and open gateway findings](../../README.md) and the
[implementation record](../plans/2026-09-02-on-device-wifi-password.md).

## Purpose

Allow a user who selects an unknown secured Wi-Fi network on the
ESP32-2432S028R to enter its password directly on the touchscreen. The
existing phone portal remains available as an explicit fallback.

This design changes credential entry only. It retains the existing selected
network policy, pending-profile validation, rollback behavior, transactional
profile storage, NAPT policy, private access point, dashboard, Modbus path,
and Venus boundary.

## Goals

- Open on-device password entry immediately after a secured, unknown scan
  result is selected.
- Provide a QWERTY letter keyboard with lowercase/uppercase Shift and two
  alternate pages covering every printable ASCII password character,
  including digits, punctuation, and space.
- Mask the password by default and provide an explicit `Show`/`Hide` toggle.
- Keep the phone portal available through a `Use phone` action.
- Retain a failed password in volatile memory, masked, so the user can correct
  a typo.
- Securely erase every credential copy on success, cancellation, timeout,
  phone fallback, or setup exit.
- Preserve existing saved profiles and touch calibration without an NVS
  migration.

## Non-goals

- Manual SSID entry; the network is still chosen from scan results.
- Unicode or locale-specific keyboard layouts.
- Cursor movement, selection, clipboard, or insertion anywhere except at the
  end of the password.
- WPA-Enterprise identity/certificate entry.
- Replacing or externally exposing the existing phone portal.
- Changes to upstream roaming, inbound forwarding, Modbus, Venus OS, or the
  private access-point credentials.

## User flow

### Secured unknown network

1. The user opens Network Setup, selects `Nearby`, and chooses an unknown
   secured SSID.
2. The password view opens immediately with the selected SSID, an empty masked
   field, lowercase QWERTY keys, `Show`, `123`, `Space`, `Use phone`, and a
   disabled `Connect` action.
3. Every accepted key updates the field and character count immediately.
4. `Shift` toggles the alphabet page between lowercase and uppercase. `123`
   opens the digits/common-symbol page; `#+=` opens the remaining-symbol page;
   `ABC` returns to the last selected alphabet case.
5. `Connect` becomes enabled at 8 characters and remains enabled through the
   63-character limit.
6. On `Connect`, the keyboard is locked and shows the existing
   connecting/validating phase while the normal pending-profile path runs.
7. Success securely clears the entry session, commits the validated profile,
   and shows the existing success result.
8. Failure returns to the keyboard with a generic error and the password still
   present but masked. The user may edit and retry.

### Phone fallback

1. `Use phone` securely clears the on-device password buffer.
2. The existing physical portal begins for the same selected SSID and security
   type.
3. The display shows the local setup URL, one-time code, and expiry exactly as
   it does today.
4. Portal submission feeds the same pending-profile validation path as an
   on-device submission.

### Open network

An unknown open network bypasses password entry and continues directly into
the existing pending-profile connection path with an empty password.

### Back and cancellation

- `Back` from password entry securely clears the session and returns to the
  current `Nearby` results.
- A second `Back` exits Network Setup through the existing behavior.
- While a connection attempt is running, `Back` remains available. It cancels
  the attempt, restores the previously active profile through the existing
  rollback path, clears the entry session, and returns to `Nearby`.

## Display and keyboard layout

The 320x240 surface retains the existing setup header and touch conventions.
The password view is arranged vertically as:

```text
[Back]          Selected SSID                         [Show]
[************************                         14/63]

 Q  W  E  R  T  Y  U  I  O  P
  A  S  D  F  G  H  J  K  L
   Z  X  C  V  B  N  M

[Shift] [123/ABC/#+=] [Space] [Backspace]
[Use phone]                              [Connect]
```

The implementation may tighten labels to fit, but the control roles and page
transitions are fixed. Interactive targets must use the corrected calibrated
coordinate contract and remain large enough for deliberate finger taps.

The alphabet pages contain standard QWERTY rows. The alternate pages together
cover digits `0` through `9` and every printable ASCII punctuation character;
the shared Space key completes the printable ASCII character set:

```text
! " # $ % & ' ( ) * + , - . / : ; < = > ? @ [ \ ] ^ _ ` { | } ~
```

Password input is exact: leading, trailing, and repeated spaces are not
trimmed. Backspace removes one byte per deliberate tap. No key-repeat or
long-press shortcut is required in the first implementation.

The field uses `*` for masking so it does not depend on an unavailable font
glyph. `Show` reveals the current password only while explicitly enabled;
leaving the view always resets visibility to masked. Rendering a masked field
must never pass the clear password to display logging/test traces.

## Components and responsibilities

### Credential entry controller

A new display-independent controller owns one entry session:

- selected SSID and scan security type;
- a fixed 64-byte, null-terminated password buffer;
- current length, alphabet case, keyboard page, and visibility;
- connection-in-progress and generic error state;
- last-activity time and five-minute entry deadline; and
- a pending-submission flag that prevents duplicate submission.

It exposes intent-level operations such as begin, append character, backspace,
toggle case, select page, toggle visibility, submit, fail, succeed, cancel, and
poll timeout. It validates state transitions and never depends on TFT_eSPI,
Wi-Fi, WebServer, Preferences, or NVS.

### Keyboard layout

Static layout data maps each page's key rectangles to semantic key actions.
Character coverage and hit-testing are pure and host-testable. Layout data does
not own the password or trigger network operations.

### Wi-Fi setup UI

`WifiSetupUi` gains password-entry and connecting views. It renders controller
state, routes touch events through the static key layout, requests repaints
only when visible state changes, and emits intent actions to the application.
It does not persist credentials or call Wi-Fi directly.

### Shared credential submission

On-device entry and `ProvisioningPortal` produce the same bounded credential
submission structure: selected SSID, password, and security type. The structure
has explicit secure-clear behavior. The application consumes and clears each
temporary submission immediately.

The on-device controller retains its separate active buffer during an attempt
so a failure can return for correction. It clears that buffer only through the
defined success/cancel/timeout/fallback paths.

### Application coordinator

The sketch continues to own network and lifecycle side effects. It sends both
submission sources into the existing `beginPendingProfile()` path and records
which source started the attempt. That source determines failure presentation:

- on-device failure restores the password view and retains the masked active
  buffer;
- portal failure continues to use the existing result view.

Success uses the existing transactional upsert-and-activate behavior, then
clears the source session. A cancelled on-device attempt uses the existing
previous-profile restoration behavior before returning to `Nearby`.

## Validation and timeouts

- Unknown protected networks accept 8 through 63 printable ASCII characters.
- The same length rule is shared by on-device entry and the phone portal.
- Unknown open networks require and carry an empty password.
- The controller rejects appends beyond 63 bytes without altering the buffer.
- Every accepted touch in the password view refreshes a five-minute inactivity
  deadline. Connection time does not consume that entry deadline.
- On-device timeout clears the session and returns to `Nearby` with a generic
  timeout message.
- The phone portal retains its existing ten-minute lifetime and network-origin
  restrictions.
- The existing general setup inactivity timeout does not close an active
  password or connecting view; those views follow the controller deadline and
  connection lifecycle instead.

## Credential handling and security

- The entry controller uses fixed storage rather than a heap-backed `String`
  for its active password.
- Password buffers are overwritten through a non-optimizable secure-clear
  function before their state is released or reused.
- No password may be written to Serial, logs, exceptions, result text, test
  failure messages, source fixtures that resemble real credentials, or tracked
  documentation.
- Masked rendering exposes only length. Visible rendering is allowed only
  after the local `Show` action and still must not pass through diagnostic
  output.
- A password is not persisted until the upstream connection reaches the
  existing successful validation outcome.
- Bad credentials must not replace or delete the previously active saved
  profile.
- `Use phone` cannot leave an on-device copy alive while the portal is active.
- Reset, setup exit, successful connection, cancellation, and timeout all
  converge on the same secure-clear operation.

## Error handling

- Invalid length leaves `Connect` disabled and shows a concise requirement,
  without echoing input.
- A full buffer ignores additional character keys while keeping Backspace,
  `Back`, and `Use phone` available.
- A connection failure shows a generic message such as `Connection failed`;
  it does not classify the password or display it.
- A submission or lifecycle invariant failure securely clears temporary
  copies, preserves the previous active profile, and presents a generic result.
- Touches during connecting cannot edit or resubmit the password. Only `Back`
  may cancel the attempt.
- Portal start failure clears local credential state before showing the
  existing generic failure result.

## Rendering and performance

- Key geometry and labels are static; no dynamic allocation is required for
  layout.
- The password field, character count, mode indicators, error line, and
  connection status may use partial repaints. Whole-screen repaint requests
  remain limited to view/page transitions.
- Stable keyboard state must not trigger periodic full-screen redraws.
- The existing asynchronous scan and Modbus worker behavior remains unchanged.

## Testing strategy

Development follows strict red-green-refactor sequencing.

### Controller host tests

- session begin/default masked state;
- lowercase/uppercase Shift behavior;
- digits and complete printable-symbol coverage across both alternate pages;
- exact space handling;
- append, one-byte Backspace, and 63-byte cap;
- 8/63 boundaries and disabled submission outside them;
- duplicate-submission prevention;
- five-minute inactivity behavior and activity refresh;
- failure retention with visibility reset to masked;
- success, Back, timeout, reset, and phone-fallback clearing; and
- open-network empty-password routing.

### UI host tests

- every key rectangle routes to the intended semantic action;
- QWERTY, uppercase, numbers, and remaining-symbol pages render correctly;
- masked output contains no clear password while visible output changes only
  after `Show`;
- `Back`, `Use phone`, Backspace, and enabled/disabled `Connect` behavior;
- connecting locks edits and duplicate submission;
- failure returns to the keyboard and success leaves it; and
- stable keyboard state avoids periodic full-screen clears.

### Integration and regression tests

- on-device and portal submissions enter the same pending-profile path;
- an on-device failure retains only the controller's masked active buffer and
  restores the previous active profile;
- success persists and activates the new profile, then clears volatile copies;
- cancellation restores the previous profile and returns to `Nearby`;
- portal code, origin, expiry, single-use, and generic-error tests remain
  passing;
- existing scan, touch, setup, profile, gateway, and Modbus suites remain
  passing; and
- secret scans, ignored-secret checks, and `git diff --check` pass.

The final verification includes every host suite with warnings treated as
errors, Arduino-ESP32 3.3.11 compilation, independent code review, a verified
NVS-preserving upload, and physical touchscreen acceptance.

## Physical acceptance

1. Selecting a protected unknown SSID opens the lowercase QWERTY keyboard.
2. Center taps reliably operate all letter and control rows.
3. Shift, `123`, `#+=`, `ABC`, Space, Backspace, and Show/Hide behave as
   specified.
4. The displayed count and 8/63 Connect enablement are correct.
5. A deliberately incorrect password returns masked and editable without
   replacing the previous profile.
6. Corrected entry connects, validates, persists, and clears the volatile
   session.
7. `Use phone` clears local entry and completes the existing portal flow.
8. `Back` from entry returns to `Nearby`; `Back` during connecting cancels and
   restores the previous profile.
9. Five-minute inactivity clears the entry and returns to `Nearby`.
10. Reboot confirms the successful profile persists while no entry UI state or
    clear password is recoverable.
