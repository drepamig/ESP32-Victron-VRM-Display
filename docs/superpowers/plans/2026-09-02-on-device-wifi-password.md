# On-device Wi-Fi Password Entry Implementation Plan

**Status, reconciled 2026-09-03:** Implemented through `1673bbc`; physical
keyboard acceptance remains pending. The task procedures and unchecked steps
below preserve the original implementation recipe, not an outstanding-work
list. Do not recreate the modules or replay old RED steps. Current findings,
remaining acceptance, and verification evidence live in
[docs/README.md](../../README.md).

| Task | Implementation evidence |
| --- | --- |
| 1: Credential contract/controller | `f44da44`; production module and host suite present. |
| 2: Keyboard layout | `de00320`; geometry and character coverage suite present. |
| 3: Password UI | `9a692fc`, followed by interaction/layout fixes through `1673bbc`. |
| 4: Shared portal submission | `77910e3`; bounded submission shared with on-device entry. |
| 5: Application integration | `22cebc3`, followed by flow fixes at `67e4555`. |
| 6: Documentation/verification | `7189268`, with later tooling and docs updates; current verification includes 15 C++ suites and 14 Python tests. |

The review at `6ef6c93` found open saved-selection persistence and WAN-outage
status defects in the retained gateway paths (R1/R2). Passing keyboard tests
does not close those findings or establish physical acceptance. Use the
[current verification commands](../../handoff/linux-2026-08-31/VERIFY_LINUX.md)
instead of the historical per-file commands below; the virtual bench added
dependencies and expanded the matrix.

**Goal:** Let a user enter an unknown protected Wi-Fi network's password on the ESP32 touchscreen while preserving the phone portal as a secure fallback.

**Architecture:** A display-independent `CredentialEntryController` owns bounded volatile password state and emits a one-shot `CredentialSubmission`. A pure keyboard-layout module maps the approved QWERTY/number/symbol geometry to semantic actions, while `WifiSetupUi` renders and routes those actions. The sketch coordinates on-device and portal submissions through the existing pending-profile lifecycle, using a recorded source to decide whether a failed connection returns to the keyboard.

**Tech Stack:** Arduino C++17, TFT_eSPI, Arduino `WebServer`, host C++17 tests built with MSVC `/W4 /WX`, Arduino-ESP32 3.3.11, XPT2046_Touchscreen 1.4.

**Spec:** `docs/superpowers/specs/2026-09-02-on-device-wifi-password-design.md`

## Global Constraints

- Work in the current repository checkout on `develop` unless the user explicitly instructs otherwise, following the root `AGENTS.md`; do not create or switch workspaces.
- Follow strict red-green-refactor: every production behavior starts with a focused failing host test that fails for the missing behavior, not a compile typo.
- Protected-network passwords are 8 through 63 printable ASCII bytes; open networks carry exactly zero password bytes.
- The on-device entry inactivity limit is exactly 300000 ms and pauses while a connection attempt is active; the phone portal remains exactly 600000 ms.
- The on-device keyboard covers QWERTY lowercase/uppercase, digits `0`-`9`, space, and all printable ASCII punctuation across `123` and `#+=` pages.
- Active and submitted credentials use fixed 64-byte null-terminated buffers and are overwritten by a non-optimizable clear before release or reuse.
- Never log, serialize, or include a clear password in assertion messages, committed fixtures that resemble real credentials, or tracked documentation.
- A failed on-device connection retains the active password only in volatile controller memory, resets it to masked, and restores the previous active profile before correction.
- Success, cancellation, timeout, phone fallback, setup exit, and reset clear on-device credential material.
- Preserve the existing profile store schema, touch calibration/NVS data, private AP/NAPT behavior, scan behavior, Modbus behavior, and Venus boundary.
- Do not modify, stage, or commit `docs/handoff.lnk` or `VictronCYD_Modbus/secrets.h`.
- Do not upload firmware or push the branch without a separate explicit authorization after review.

---

### Task 1: Bounded credential contract and entry controller

**Files:**
- Create: `VictronCYD_Modbus/CredentialSubmission.h`
- Create: `VictronCYD_Modbus/CredentialEntryController.h`
- Create: `VictronCYD_Modbus/CredentialEntryController.cpp`
- Create: `tests/host/credential_entry_controller_test.cpp`

**Interfaces:**
- Produces: `CredentialSubmission`, `credentialPassphraseValid(uint8_t, const char*, size_t)`, and `CredentialEntryController`.
- Produces controller methods used by later tasks: `begin`, `append`, `backspace`, `toggleShift`, `selectPage`, `toggleVisibility`, `submit`, `takeSubmission`, `connectionFailed`, `succeed`, `cancel`, `pollTimeout`, `copyDisplayText`, and read-only state accessors.

- [ ] **Step 1: Write the failing controller and submission tests**

Create table-driven tests whose literal expectations cover protected 7/8/63/64-byte boundaries, open-network empty-only validation, printable ASCII rejection, default lowercase/masked state, exact append/backspace/space behavior, the 63-byte cap, page/case state, one-shot submission, duplicate-submit rejection, failed-connection retention with masking restored, 300000 ms inactivity including `uint32_t` wraparound, and clearing on success/cancel/timeout/fallback.

```cpp
CredentialEntryController entry;
check(entry.begin("SyntheticNet", 3, 100), "protected selection starts entry");
for (char value : std::string("A1!A1!A1!")) {
  check(entry.append(value, 101), "printable synthetic byte appends");
}
CredentialSubmission submission;
check(entry.submit(200) && entry.takeSubmission(submission),
      "valid entry produces one bounded submission");
check(submission.ready && std::string(submission.ssid) == "SyntheticNet" &&
          std::string(submission.passphrase) == "A1!A1!A1!" &&
          submission.securityType == 3,
      "submission preserves exact selected values");
check(!entry.submit(201) && !entry.takeSubmission(submission),
      "connecting entry cannot submit twice");
submission.clear();
entry.connectionFailed(1000);
check(entry.active() && !entry.connecting() && !entry.visible() && entry.length() == 9,
      "connection failure retains only masked editable state");
```

The timeout test must mutate correctly at both sides of the deadline:

```cpp
CredentialEntryController timeout;
check(timeout.begin("TimeoutNet", 3, UINT32_MAX - 100), "wrapped entry starts");
check(!timeout.pollTimeout(299898) && timeout.active(), "timeout does not fire early");
check(timeout.pollTimeout(299899) && !timeout.active(), "timeout fires at 300000 ms");
```

- [ ] **Step 2: Compile and run the focused test to verify RED**

Run from a Visual Studio x64 developer environment:

```powershell
cl.exe /nologo /std:c++17 /W4 /WX /EHsc /Itests\host\fakes /IVictronCYD_Modbus tests\host\credential_entry_controller_test.cpp VictronCYD_Modbus\CredentialEntryController.cpp /Febuild\credential_entry_controller_test.exe
.\build\credential_entry_controller_test.exe
```

Expected: compilation fails because `CredentialSubmission.h` and `CredentialEntryController.h` do not exist. Add only empty declarations needed to compile, rerun, and confirm behavioral assertions fail because entry/submission behavior is absent.

- [ ] **Step 3: Implement the fixed submission contract**

Implement `CredentialSubmission` as non-copyable fixed storage with an idempotent `clear()` and destructor. The validation helper must reject bytes outside `0x20..0x7e`, enforce an empty password for security type zero, and enforce 8..63 bytes otherwise.

```cpp
struct CredentialSubmission {
  char ssid[33] = {};
  char passphrase[64] = {};
  uint8_t securityType = 0;
  bool ready = false;

  CredentialSubmission() = default;
  CredentialSubmission(const CredentialSubmission&) = delete;
  CredentialSubmission& operator=(const CredentialSubmission&) = delete;
  ~CredentialSubmission() { clear(); }

  bool set(const char* selectedSsid, const char* selectedPassphrase, uint8_t selectedSecurityType);
  void clear();
};

inline void secureClearBytes(void* value, size_t length) {
  volatile uint8_t* cursor = static_cast<volatile uint8_t*>(value);
  while (length-- > 0) *cursor++ = 0;
}
```

- [ ] **Step 4: Implement the controller state machine minimally**

Use this public contract and keep all password storage inside `password_[64]`:

```cpp
enum class CredentialKeyboardPage : uint8_t { Alphabet, Numbers, Symbols };

class CredentialEntryController {
 public:
  static constexpr uint32_t kInactivityMs = 300000;
  ~CredentialEntryController() { cancel(); }
  bool begin(const char* ssid, uint8_t securityType, uint32_t nowMs);
  bool append(char value, uint32_t nowMs);
  bool backspace(uint32_t nowMs);
  bool toggleShift(uint32_t nowMs);
  bool selectPage(CredentialKeyboardPage page, uint32_t nowMs);
  bool toggleVisibility(uint32_t nowMs);
  bool submit(uint32_t nowMs);
  bool takeSubmission(CredentialSubmission& out);
  void connectionFailed(uint32_t nowMs);
  void succeed();
  void cancel();
  bool pollTimeout(uint32_t nowMs);
  size_t copyDisplayText(char* output, size_t capacity) const;
  bool active() const;
  bool connecting() const;
  bool visible() const;
  bool uppercase() const;
  bool hasError() const;
  bool canSubmit() const;
  size_t length() const;
  const char* ssid() const;
  uint8_t securityType() const;
  CredentialKeyboardPage page() const;
};
```

`submit()` sets `connecting_` and a one-shot `submissionReady_` without clearing the active password. `takeSubmission()` calls `out.set(...)` and consumes only `submissionReady_`. `connectionFailed(nowMs)` restarts the inactivity window, clears the connecting/submission flags, sets the generic-error flag, and forces masking. `pollTimeout()` does nothing while connecting and otherwise compares elapsed unsigned time against 300000 ms.

- [ ] **Step 5: Run focused tests and refactor while green**

```powershell
cl.exe /nologo /std:c++17 /W4 /WX /EHsc /Itests\host\fakes /IVictronCYD_Modbus tests\host\credential_entry_controller_test.cpp VictronCYD_Modbus\CredentialEntryController.cpp /Febuild\credential_entry_controller_test.exe
.\build\credential_entry_controller_test.exe
```

Expected: exit 0 with no warnings or credential text in output. Confirm each realistic mutation—wrong minimum, missing ASCII check, missing one-shot flag, timeout off by one, and missing clear—breaks at least one assertion.

- [ ] **Step 6: Commit Task 1**

```powershell
git add VictronCYD_Modbus/CredentialSubmission.h VictronCYD_Modbus/CredentialEntryController.h VictronCYD_Modbus/CredentialEntryController.cpp tests/host/credential_entry_controller_test.cpp
git commit -m "feat: add bounded WiFi credential entry state"
```

### Task 2: Deterministic QWERTY, number, and symbol layout

**Files:**
- Create: `VictronCYD_Modbus/CredentialKeyboardLayout.h`
- Create: `VictronCYD_Modbus/CredentialKeyboardLayout.cpp`
- Create: `tests/host/credential_keyboard_layout_test.cpp`

**Interfaces:**
- Consumes: `CredentialKeyboardPage` from Task 1 and `TouchPoint` from `TouchMapping.h`.
- Produces: `CredentialKeyType`, `CredentialKeyHit`, `CredentialKeyboardRow`, `credentialKeyboardRow`, `credentialKeyboardHitTest`, and fixed control bounds/labels used by `WifiSetupUi`.

- [ ] **Step 1: Write failing geometry, transition, and coverage tests**

Tests must tap the literal center of every rendered character and control rectangle, verify row characters independently against the approved literals, and accumulate exactly the printable ASCII set without deriving expectations from production layout helpers.

```cpp
const char* expectedAlphabet[] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
const char* expectedNumbers[] = {"1234567890", "-/:;()$&@\"", ".,?!'#%"};
const char* expectedSymbols[] = {"*+=<>[]{}\\", "^_`|~", ""};
```

Verify utility behavior with literal taps: alphabet exposes `Shift` and `123`; numbers expose `ABC` and `#+=`; symbols expose `ABC` and `123`; all pages expose Space, Backspace, Back, Show/Hide, Use phone, and Connect. Boundary tests must prove adjacent key rectangles do not overlap and a point immediately outside each screen edge yields `None`.

- [ ] **Step 2: Compile and run the focused test to verify RED**

```powershell
cl.exe /nologo /std:c++17 /W4 /WX /EHsc /Itests\host\fakes /IVictronCYD_Modbus tests\host\credential_keyboard_layout_test.cpp VictronCYD_Modbus\CredentialKeyboardLayout.cpp /Febuild\credential_keyboard_layout_test.exe
.\build\credential_keyboard_layout_test.exe
```

Expected: compilation fails because the layout module does not exist. After adding declarations, assertions fail because no geometry maps to the expected keys.

- [ ] **Step 3: Implement static layout and semantic hit testing**

Use fixed 320x240 geometry. The exact character rows are the literals from Step 1. Center each row of 29-pixel keys with a 2-pixel gap in the 312-pixel content area at y=76, 103, and 130 with height 24. Use these control rectangles:

```cpp
constexpr CredentialKeyRect kCredentialBackBounds{4, 4, 56, 28};
constexpr CredentialKeyRect kCredentialShowBounds{252, 4, 64, 28};
constexpr CredentialKeyRect kCredentialShiftOrAbcBounds{4, 157, 54, 30};
constexpr CredentialKeyRect kCredentialPageBounds{62, 157, 54, 30};
constexpr CredentialKeyRect kCredentialSpaceBounds{120, 157, 132, 30};
constexpr CredentialKeyRect kCredentialBackspaceBounds{256, 157, 60, 30};
constexpr CredentialKeyRect kCredentialUsePhoneBounds{4, 191, 151, 45};
constexpr CredentialKeyRect kCredentialConnectBounds{164, 191, 152, 45};
```

Expose row geometry instead of duplicating it in UI code:

```cpp
struct CredentialKeyboardRow {
  const char* characters;
  int16_t x;
  int16_t y;
  int16_t keyWidth;
  int16_t keyHeight;
  int16_t gap;
};

CredentialKeyboardRow credentialKeyboardRow(CredentialKeyboardPage page, size_t row);
CredentialKeyHit credentialKeyboardHitTest(CredentialKeyboardPage page,
                                           const TouchPoint& point);
const char* credentialShiftOrAbcLabel(CredentialKeyboardPage page);
const char* credentialPageLabel(CredentialKeyboardPage page);
```

For alphabet hits, return lowercase character bytes; `WifiSetupUi` applies controller uppercase state before append. For alternate pages, return the exact punctuation byte unchanged.

- [ ] **Step 4: Run focused tests and refactor while green**

```powershell
cl.exe /nologo /std:c++17 /W4 /WX /EHsc /Itests\host\fakes /IVictronCYD_Modbus tests\host\credential_keyboard_layout_test.cpp VictronCYD_Modbus\CredentialKeyboardLayout.cpp /Febuild\credential_keyboard_layout_test.exe
.\build\credential_keyboard_layout_test.exe
```

Expected: exit 0 with no warnings. Confirm removing any character, shifting a control rectangle into a neighbor, or swapping a mode action breaks a literal test.

- [ ] **Step 5: Commit Task 2**

```powershell
git add VictronCYD_Modbus/CredentialKeyboardLayout.h VictronCYD_Modbus/CredentialKeyboardLayout.cpp tests/host/credential_keyboard_layout_test.cpp
git commit -m "feat: add touchscreen WiFi keyboard layout"
```

### Task 3: Password and connecting views in `WifiSetupUi`

**Files:**
- Modify: `VictronCYD_Modbus/WifiSetupUi.h`
- Modify: `VictronCYD_Modbus/WifiSetupUi.cpp`
- Modify: `tests/host/wifi_setup_ui_test.cpp`

**Interfaces:**
- Consumes: Task 1 controller/submission and Task 2 layout/hit testing.
- Produces new actions `SubmitCredentials`, `UsePhone`, and `CancelCredentialAttempt`.
- Produces UI methods `showCredentialEntry`, `takeCredentialSubmission`, `showCredentialFailure`, and `cancelCredentialAttempt` for Task 5.

- [ ] **Step 1: Write failing UI routing and rendering tests**

Extend the real `TFT_eSPI` fake-driven suite. Drive the UI from `Nearby` into an unknown protected network, call `showCredentialEntry`, and assert a single full repaint contains the selected SSID, `Show`, QWERTY labels, `123`, `Space`, `Use phone`, and a disabled-looking Connect control. Type a literal synthetic value by tapping key centers and verify rendering contains only stars and `9/63`, never the clear fixture, until Show is tapped.

```cpp
ui.showCredentialEntry("SyntheticNet", 3, 100);
ui.render(offlineStatus());
check(display.drewContaining("SyntheticNet") && display.drew("Show") &&
          display.drew("Use phone"),
      "protected selection renders local credential entry");
// Tap character centers supplied as literal points from the Task 2 geometry.
check(!display.drewContaining("A1!A1!A1!"),
      "masked entry never sends the clear fixture to the display");
```

Cover Shift, `123`, `#+=`, `ABC`, exact Space, Backspace, disabled Connect below 8 bytes, one `SubmitCredentials` action at 8 bytes, locked edits while connecting, `CancelCredentialAttempt` from connecting Back, failed return with retained masked length/error, `UsePhone` carrying only SSID/security after clearing entry, entry Back returning to current Nearby results, and no periodic `fillScreen` calls in stable keyboard state.

Add deadline assertions proving ordinary 60000 ms setup inactivity does not close Password/Connecting, password inactivity returns to Nearby at 300000 ms with a visible `Entry timed out` notice, and connection time does not consume the entry deadline.

- [ ] **Step 2: Compile and run UI tests to verify RED**

```powershell
cl.exe /nologo /std:c++17 /W4 /WX /EHsc /Itests\host\fakes /IVictronCYD_Modbus tests\host\wifi_setup_ui_test.cpp VictronCYD_Modbus\WifiSetupUi.cpp VictronCYD_Modbus\CredentialEntryController.cpp VictronCYD_Modbus\CredentialKeyboardLayout.cpp /Febuild\wifi_setup_ui_test.exe
.\build\wifi_setup_ui_test.exe
```

Expected: compilation fails on the new views/actions/methods. After declarations are present, focused assertions fail because the keyboard is not rendered or routed.

- [ ] **Step 3: Add credential views and actions**

Extend the enums and public API exactly:

```cpp
enum class WifiSetupView : uint8_t {
  Closed, Saved, Scanning, Nearby, ConfirmDelete, Password, Connecting, Portal, Result
};

enum class WifiSetupActionType : uint8_t {
  None, ConnectSaved, ProvisionNew, SubmitCredentials, UsePhone,
  CancelCredentialAttempt, DeleteSaved, Refresh, ClearAll, Exit
};

void showCredentialEntry(const String& ssid, uint8_t securityType, uint32_t nowMs);
bool takeCredentialSubmission(CredentialSubmission& out);
bool showCredentialFailure(const String& message, uint32_t nowMs);
void cancelCredentialAttempt();
```

Keep `CredentialEntryController credentialEntry_` and `String nearbyNotice_` as private members. `close`, `open`, `showPortal`, and successful/result transitions clear credential state. Password Back cancels locally and returns to `Nearby`; Connecting Back emits `CancelCredentialAttempt` without editing state. `UsePhone` copies only SSID/security into the action before canceling locally.

- [ ] **Step 4: Render and route the fixed keyboard**

Render the selected SSID between Back and Show/Hide, a 312x24 field at y=39 with clipped tail text and an independent `N/63` count, an error/status line at y=66, the three Task 2 rows, utility controls, and footer actions. Use `*` masking and never construct a clear `String` merely for drawing. Draw Connect selected/enabled only when length is 8..63 and not connecting.

`renderDynamic` may repaint only field/count/status controls whose visible state changed; it must not call `fillScreen` for unchanged password state. Whole-screen repaints are reserved for entering/leaving the view or changing keyboard page.

- [ ] **Step 5: Run UI and existing interaction regressions**

```powershell
cl.exe /nologo /std:c++17 /W4 /WX /EHsc /Itests\host\fakes /IVictronCYD_Modbus tests\host\wifi_setup_ui_test.cpp VictronCYD_Modbus\WifiSetupUi.cpp VictronCYD_Modbus\CredentialEntryController.cpp VictronCYD_Modbus\CredentialKeyboardLayout.cpp /Febuild\wifi_setup_ui_test.exe
.\build\wifi_setup_ui_test.exe
.\build\password-entry-baseline\touch_mapping_test.exe
.\build\password-entry-baseline\touch_input_release_test.exe
```

Expected: all three executables exit 0. Confirm masking/removal, Connect gating, Back routing, or full-screen repaint mutations each break a focused UI assertion.

- [ ] **Step 6: Commit Task 3**

```powershell
git add VictronCYD_Modbus/WifiSetupUi.h VictronCYD_Modbus/WifiSetupUi.cpp tests/host/wifi_setup_ui_test.cpp
git commit -m "feat: add on-device WiFi password UI"
```

### Task 4: Unify phone portal submissions and validation

**Files:**
- Modify: `VictronCYD_Modbus/ProvisioningPortal.h`
- Modify: `VictronCYD_Modbus/ProvisioningPortal.cpp`
- Modify: `tests/host/provisioning_portal_test.cpp`

**Interfaces:**
- Consumes: `CredentialSubmission` and `credentialPassphraseValid` from Task 1.
- Produces: `bool ProvisioningPortal::takeSubmission(CredentialSubmission& out)` for Task 5.

- [ ] **Step 1: Write failing portal contract tests**

Replace `ProvisioningSubmission` expectations with the fixed shared structure. Add literal cases proving protected lengths 0..7 and 64 are rejected, 8 and 63 are accepted, non-printable bytes are rejected generically, open networks accept only empty passwords, transfer is one-shot, and repeated begin/cancel/timeout clear prior pending material.

Use obviously synthetic fixtures such as repeated `'x'` bytes or `A1!A1!A1!`; no assertion message may include the submitted value.

```cpp
CredentialSubmission accepted;
check(portal.takeSubmission(accepted) && accepted.ready &&
          std::string(accepted.ssid) == "SyntheticNet" &&
          std::string(accepted.passphrase) == "A1!A1!A1!" &&
          accepted.securityType == 3,
      "portal transfers one bounded shared submission");
accepted.clear();
```

- [ ] **Step 2: Compile and run portal tests to verify RED**

```powershell
cl.exe /nologo /std:c++17 /W4 /WX /EHsc /Itests\host\fakes /IVictronCYD_Modbus tests\host\provisioning_portal_test.cpp VictronCYD_Modbus\ProvisioningPortal.cpp /Febuild\provisioning_portal_test.exe
.\build\provisioning_portal_test.exe
```

Expected: compilation fails because the old `ProvisioningSubmission` contract still exists; after signature changes, boundary assertions fail under the old non-empty protected rule.

- [ ] **Step 3: Route portal data through `CredentialSubmission`**

Remove `ProvisioningSubmission`. Keep the portal's active/pending fixed arrays, but make `takeSubmission` call `out.set(pendingSsid_, pendingPassphrase_, pendingSecurityType_)` before clearing the pending buffers. In `handlePost`, validate with `credentialPassphraseValid` and securely overwrite the local mutable Arduino `String` copy on every success and rejection path after it is read.

```cpp
String submittedPassphrase = server_.hasArg("password") ? server_.arg("password") : String();
const bool valid = credentialPassphraseValid(selectedSecurityType_,
                                             submittedPassphrase.c_str(),
                                             submittedPassphrase.length());
if (!valid) {
  secureClearString(submittedPassphrase);
  server_.send(400, "text/plain", "Request rejected.");
  return;
}
```

- [ ] **Step 4: Run focused and controller tests**

```powershell
cl.exe /nologo /std:c++17 /W4 /WX /EHsc /Itests\host\fakes /IVictronCYD_Modbus tests\host\provisioning_portal_test.cpp VictronCYD_Modbus\ProvisioningPortal.cpp /Febuild\provisioning_portal_test.exe
.\build\provisioning_portal_test.exe
.\build\credential_entry_controller_test.exe
```

Expected: both exit 0 with no warnings and no credential text in output.

- [ ] **Step 5: Commit Task 4**

```powershell
git add VictronCYD_Modbus/ProvisioningPortal.h VictronCYD_Modbus/ProvisioningPortal.cpp tests/host/provisioning_portal_test.cpp
git commit -m "refactor: share bounded WiFi credential submissions"
```

### Task 5: Integrate on-device credentials with pending-profile lifecycle

**Files:**
- Modify: `VictronCYD_Modbus/GatewayApplicationPolicy.h`
- Modify: `tests/host/gateway_application_policy_test.cpp`
- Modify: `VictronCYD_Modbus/VictronCYD_Modbus.ino`

**Interfaces:**
- Consumes: shared submissions from Tasks 1/4 and UI actions/methods from Task 3.
- Extends: `GatewayLifecyclePolicy::replaceWith` with a `PendingProfileSource` argument and `pendingSource()` accessor.
- Produces: one coordinator path for open, portal, and on-device credential submissions.

- [ ] **Step 1: Write failing lifecycle-source tests**

Change protected unknown routing to on-device entry and add source ownership tests:

```cpp
enum class ProvisioningRoute : uint8_t { DirectPending, OnDevicePassword };
enum class PendingProfileSource : uint8_t { None, DirectOpen, Portal, OnDevice };

check(provisioningRouteForSecurity(0) == ProvisioningRoute::DirectPending,
      "unknown open network bypasses password entry");
check(provisioningRouteForSecurity(3) == ProvisioningRoute::OnDevicePassword,
      "unknown protected network opens on-device password entry");

GatewayLifecyclePolicy lifecycle;
lifecycle.replaceWith(GatewayLifecycleTarget::PendingProfile, 100, 2,
                      PendingProfileSource::OnDevice);
check(lifecycle.pendingSource() == PendingProfileSource::OnDevice,
      "pending attempt records on-device presentation ownership");
lifecycle.completePending();
check(lifecycle.pendingSource() == PendingProfileSource::None,
      "completion clears pending source ownership");
```

Also verify replacing pending with portal/open/saved targets cannot retain a stale source.

- [ ] **Step 2: Compile and run the lifecycle test to verify RED**

```powershell
cl.exe /nologo /std:c++17 /W4 /WX /EHsc /Itests\host\fakes /IVictronCYD_Modbus tests\host\gateway_application_policy_test.cpp /Febuild\gateway_application_policy_test.exe
.\build\gateway_application_policy_test.exe
```

Expected: compile or assertion failure because protected routing still selects `PhysicalPortal` and pending source is not tracked.

- [ ] **Step 3: Implement pending source policy minimally**

Add `PendingProfileSource` storage to `GatewayLifecyclePolicy`. `replaceWith(..., source)` records a non-`None` source only for `PendingProfile`; every other target and `completePending()` resets it. Preserve existing pending deadline, replacement, cancellation, and Exit semantics.

- [ ] **Step 4: Route all new UI actions in the sketch**

Replace the passphrase-`String` coordinator entry with a bounded submission entry:

```cpp
void beginPendingProfile(CredentialSubmission& submission,
                         PendingProfileSource source,
                         uint32_t nowMs);
```

This function copies the bounded values into `pendingProfile`, immediately calls `submission.clear()`, records `source`, preserves the previous profile, keeps the setup view open only for `OnDevice`, and starts `camperNetwork.connect` through the current lifecycle.

Route actions as follows:

```cpp
case WifiSetupActionType::ProvisionNew:
  if (provisioningRouteForSecurity(action.securityType) == ProvisioningRoute::DirectPending) {
    CredentialSubmission submission;
    if (submission.set(action.ssid.c_str(), "", action.securityType))
      beginPendingProfile(submission, PendingProfileSource::DirectOpen, nowMs);
  } else {
    wifiSetupUi.showCredentialEntry(action.ssid, action.securityType, nowMs);
    wifiSetupUi.render(camperNetwork.status());
  }
  break;
case WifiSetupActionType::SubmitCredentials: {
  CredentialSubmission submission;
  if (wifiSetupUi.takeCredentialSubmission(submission))
    beginPendingProfile(submission, PendingProfileSource::OnDevice, nowMs);
  break;
}
case WifiSetupActionType::UsePhone:
  startPhysicalPortal(action.ssid, action.securityType, nowMs);
  break;
case WifiSetupActionType::CancelCredentialAttempt:
  cancelOnDevicePending(nowMs);
  break;
```

Keep the two side-effect helpers explicit:

```cpp
void startPhysicalPortal(const String& ssid, uint8_t securityType, uint32_t nowMs) {
  replaceGatewayLifecycle(GatewayLifecycleTarget::PhysicalPortal);
  if (portal.begin(ssid, securityType, nowMs)) {
    wifiSetupUi.showPortal(ssid, portal.pairingCode(), portal.expiresAtMs());
  } else {
    replaceGatewayLifecycle(GatewayLifecycleTarget::Idle);
    wifiSetupUi.showResult("Setup portal failed", false);
  }
  wifiSetupUi.render(camperNetwork.status());
}

void cancelOnDevicePending(uint32_t nowMs) {
  const PendingProfileEvaluation evaluation = gatewayLifecycle.pendingImmediateFailure();
  camperNetwork.cancelPendingProfile();
  if (evaluation.outcome == PendingProfileOutcome::RestorePrevious) {
    reconnectPreviousProfile(nowMs, evaluation.previousActiveIndex);
  }
  gatewayLifecycle.completePending();
  clearPendingApplicationBuffers();
  refreshSavedProfiles();
  wifiSetupUi.cancelCredentialAttempt();
  wifiSetupUi.render(camperNetwork.status());
}
```

`cancelOnDevicePending` evaluates the current pending attempt before completing
it, calls `camperNetwork.cancelPendingProfile()`, reconnects the retained
previous profile when the evaluation says `RestorePrevious`, completes the
gateway lifecycle, clears pending application buffers and controller state,
refreshes saved profiles, returns the UI to Nearby, and renders it.

Portal submissions call the same `beginPendingProfile(submission, PendingProfileSource::Portal, nowMs)`. Connection rejection/timeout reads `gatewayLifecycle.pendingSource()` before completion: `OnDevice` restores the previous profile and calls `wifiSetupUi.showCredentialFailure("Connection failed", nowMs)`; other sources use the existing generic result view. Persistence/invariant failure clears the controller and shows the generic result instead of retaining credentials. Success clears controller state and shows the existing `Upstream connected` result.

- [ ] **Step 5: Run lifecycle, UI, controller, and portal suites**

Recompile each suite against current sources, then run:

```powershell
cl.exe /nologo /std:c++17 /W4 /WX /EHsc /Itests\host\fakes /IVictronCYD_Modbus tests\host\gateway_application_policy_test.cpp /Febuild\gateway_application_policy_test.exe
cl.exe /nologo /std:c++17 /W4 /WX /EHsc /Itests\host\fakes /IVictronCYD_Modbus tests\host\credential_entry_controller_test.cpp VictronCYD_Modbus\CredentialEntryController.cpp /Febuild\credential_entry_controller_test.exe
cl.exe /nologo /std:c++17 /W4 /WX /EHsc /Itests\host\fakes /IVictronCYD_Modbus tests\host\credential_keyboard_layout_test.cpp VictronCYD_Modbus\CredentialKeyboardLayout.cpp /Febuild\credential_keyboard_layout_test.exe
cl.exe /nologo /std:c++17 /W4 /WX /EHsc /Itests\host\fakes /IVictronCYD_Modbus tests\host\wifi_setup_ui_test.cpp VictronCYD_Modbus\WifiSetupUi.cpp VictronCYD_Modbus\CredentialEntryController.cpp VictronCYD_Modbus\CredentialKeyboardLayout.cpp /Febuild\wifi_setup_ui_test.exe
cl.exe /nologo /std:c++17 /W4 /WX /EHsc /Itests\host\fakes /IVictronCYD_Modbus tests\host\provisioning_portal_test.cpp VictronCYD_Modbus\ProvisioningPortal.cpp /Febuild\provisioning_portal_test.exe
.\build\gateway_application_policy_test.exe
.\build\credential_entry_controller_test.exe
.\build\credential_keyboard_layout_test.exe
.\build\wifi_setup_ui_test.exe
.\build\provisioning_portal_test.exe
```

Expected: all exit 0. Inspect the sketch diff to verify every local `CredentialSubmission` is cleared immediately and no clear password is printed.

- [ ] **Step 6: Compile the full firmware**

```powershell
arduino-cli compile --warnings all --fqbn esp32:esp32:esp32 --build-path build\password-entry-review VictronCYD_Modbus
```

Expected: exit 0 on Arduino-ESP32 3.3.11. The documented TFT_eSPI `TOUCH_CS` warning is expected because XPT2046 uses its own SPI library; no other new warning is accepted.

- [ ] **Step 7: Commit Task 5**

```powershell
git add VictronCYD_Modbus/GatewayApplicationPolicy.h tests/host/gateway_application_policy_test.cpp VictronCYD_Modbus/VictronCYD_Modbus.ino
git commit -m "feat: connect touchscreen WiFi credentials"
```

### Task 6: Full regression, security audit, and operator documentation

**Files:**
- Modify: `README.md`
- Modify: `docs/handoff/linux-2026-08-31/VERIFY_LINUX.md`

**Interfaces:**
- Consumes: the completed on-device and portal workflow.
- Produces: reproducible build/test instructions including the two new host suites and the changed Wi-Fi setup behavior.

- [ ] **Step 1: Update user and verification documentation**

Document that selecting an unknown protected SSID opens the on-device keyboard, `Use phone` starts the private portal fallback, passwords are masked by default, and failed connection attempts return masked for correction. Add compile/run commands for `credential_entry_controller_test.cpp` and `credential_keyboard_layout_test.cpp`, and add Task 1/2 sources to the UI suite compile line.

```markdown
- Touch Wi-Fi setup supports direct masked QWERTY password entry for unknown
  protected networks, with the private phone portal available through
  **Use phone**.
```

- [ ] **Step 2: Run all eleven host suites from fresh binaries**

Compile with `/std:c++17 /W4 /WX /EHsc` and execute:

```powershell
cl.exe /nologo /std:c++17 /W4 /WX /EHsc /Itests\host\fakes /IVictronCYD_Modbus tests\host\gateway_policy_test.cpp /Febuild\host-final\gateway_policy_test.exe
cl.exe /nologo /std:c++17 /W4 /WX /EHsc /Itests\host\fakes /IVictronCYD_Modbus tests\host\gateway_application_policy_test.cpp /Febuild\host-final\gateway_application_policy_test.exe
cl.exe /nologo /std:c++17 /W4 /WX /EHsc /Itests\host\fakes /IVictronCYD_Modbus tests\host\modbus_snapshot_policy_test.cpp /Febuild\host-final\modbus_snapshot_policy_test.exe
cl.exe /nologo /std:c++17 /W4 /WX /EHsc /Itests\host\fakes /IVictronCYD_Modbus tests\host\touch_mapping_test.cpp /Febuild\host-final\touch_mapping_test.exe
cl.exe /nologo /std:c++17 /W4 /WX /EHsc /Itests\host\fakes /IVictronCYD_Modbus tests\host\network_profiles_test.cpp VictronCYD_Modbus\NetworkProfiles.cpp /Febuild\host-final\network_profiles_test.exe
cl.exe /nologo /std:c++17 /W4 /WX /EHsc /Itests\host\fakes /IVictronCYD_Modbus tests\host\camper_network_test.cpp VictronCYD_Modbus\CamperNetwork.cpp /Febuild\host-final\camper_network_test.exe
cl.exe /nologo /std:c++17 /W4 /WX /EHsc /Itests\host\fakes /IVictronCYD_Modbus tests\host\touch_input_release_test.cpp VictronCYD_Modbus\TouchInput.cpp /Febuild\host-final\touch_input_release_test.exe
cl.exe /nologo /std:c++17 /W4 /WX /EHsc /Itests\host\fakes /IVictronCYD_Modbus tests\host\credential_entry_controller_test.cpp VictronCYD_Modbus\CredentialEntryController.cpp /Febuild\host-final\credential_entry_controller_test.exe
cl.exe /nologo /std:c++17 /W4 /WX /EHsc /Itests\host\fakes /IVictronCYD_Modbus tests\host\credential_keyboard_layout_test.cpp VictronCYD_Modbus\CredentialKeyboardLayout.cpp /Febuild\host-final\credential_keyboard_layout_test.exe
cl.exe /nologo /std:c++17 /W4 /WX /EHsc /Itests\host\fakes /IVictronCYD_Modbus tests\host\wifi_setup_ui_test.cpp VictronCYD_Modbus\WifiSetupUi.cpp VictronCYD_Modbus\CredentialEntryController.cpp VictronCYD_Modbus\CredentialKeyboardLayout.cpp /Febuild\host-final\wifi_setup_ui_test.exe
cl.exe /nologo /std:c++17 /W4 /WX /EHsc /Itests\host\fakes /IVictronCYD_Modbus tests\host\provisioning_portal_test.cpp VictronCYD_Modbus\ProvisioningPortal.cpp /Febuild\host-final\provisioning_portal_test.exe

Get-ChildItem build\host-final\*.exe | ForEach-Object { & $_.FullName; if ($LASTEXITCODE -ne 0) { throw "$($_.Name) failed" } }
```

Expected: 11/11 compile and run successfully with zero unexpected warnings.

- [ ] **Step 3: Run the final firmware and repository checks**

```powershell
arduino-cli compile --warnings all --fqbn esp32:esp32:esp32 --build-path build\password-entry-final VictronCYD_Modbus
git diff --check
git status --short --branch
git check-ignore -v VictronCYD_Modbus/secrets.h build/
rg -n -i "pass(word|phrase)?.*(Serial|printf|println)|Tightbeam|COM3|192\.168\.1\.122|24:dc" VictronCYD_Modbus tests README.md docs/superpowers
```

Expected: firmware exits 0 with only the documented TFT_eSPI warning; diff check is empty; only intended tracked files plus the untouched untracked `docs/handoff.lnk` appear; both local secret and build paths are ignored; the source scan finds no clear credential logging or environment-specific sensitive values in the new change.

- [ ] **Step 4: Commit Task 6**

```powershell
git add README.md docs/handoff/linux-2026-08-31/VERIFY_LINUX.md
git commit -m "docs: describe touchscreen WiFi credential entry"
```

- [ ] **Step 5: Request final review before any hardware upload**

Review the complete branch diff against the design spec, including deferred task-review findings. Fix every Critical or Important issue, rerun its covering suites, and run one scoped re-review. Do not upload or push at this step.

### Hardware checkpoint after reviewed implementation

With separate authorization, upload the reviewed build using `--verify` without erasing NVS. Confirm the ten physical acceptance checks in the design spec: protected selection, all keyboard pages, count and Connect gating, masked failure correction, successful persistence, phone fallback, Back/cancel restoration, five-minute timeout, and reboot persistence/credential clearing.
