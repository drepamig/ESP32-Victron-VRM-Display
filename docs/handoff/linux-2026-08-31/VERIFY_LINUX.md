# Linux setup and verification

These commands rebuild the repository state on Linux. Adjust only the serial
port and the local TFT_eSPI configuration.

## Toolchain

Install Arduino CLI, a C++17 compiler, Git, and the pinned ESP32 dependencies:

```bash
arduino-cli core update-index
arduino-cli core install esp32:esp32@3.3.11
arduino-cli lib install "TFT_eSPI"
arduino-cli lib install "XPT2046_Touchscreen@1.4"
arduino-cli core list
arduino-cli lib list
```

Configure TFT_eSPI for the ESP32-2432S028R exactly as documented in the root
`README.md`. The XPT2046 controller uses its own SPI bus and library; the
TFT_eSPI `TOUCH_CS` warning is therefore expected for this project.

If the serial device is permission-denied, use the serial-access group or udev
rule appropriate for the Linux distribution, then log out and back in. Do not
run the desktop app or the whole development workflow as root.

## Repository and secret checks

Work on `develop` unless the user explicitly instructs otherwise. Verify the
current branch before continuing and follow the root `AGENTS.md` if it differs.

```bash
git branch --show-current
git status --short --branch
git log --oneline --decorate -12
git merge-base --is-ancestor a6827e6e92db1870f70ccacd73f8d2b0cf4d5a20 HEAD
git remote -v

cp -n VictronCYD_Modbus/secrets.example.h VictronCYD_Modbus/secrets.h
git check-ignore -v VictronCYD_Modbus/secrets.h
```

Populate `secrets.h` locally or transfer it privately. Never print it while
recording terminal output.

## Host suites

Run from the repository root:

```bash
set -euo pipefail
mkdir -p build/host
CXXFLAGS=(-std=c++17 -Wall -Wextra -Werror)
INCLUDES=(-Itests/host/fakes -IVictronCYD_Modbus)

g++ "${CXXFLAGS[@]}" -IVictronCYD_Modbus \
  tests/host/gateway_policy_test.cpp \
  -o build/host/gateway_policy_test
./build/host/gateway_policy_test

g++ "${CXXFLAGS[@]}" -IVictronCYD_Modbus \
  tests/host/gateway_application_policy_test.cpp \
  -o build/host/gateway_application_policy_test
./build/host/gateway_application_policy_test

g++ "${CXXFLAGS[@]}" -IVictronCYD_Modbus \
  tests/host/modbus_snapshot_policy_test.cpp \
  -o build/host/modbus_snapshot_policy_test
./build/host/modbus_snapshot_policy_test

g++ "${CXXFLAGS[@]}" -IVictronCYD_Modbus \
  tests/host/touch_mapping_test.cpp \
  -o build/host/touch_mapping_test
./build/host/touch_mapping_test

g++ "${CXXFLAGS[@]}" "${INCLUDES[@]}" \
  tests/host/network_profiles_test.cpp VictronCYD_Modbus/NetworkProfiles.cpp \
  -o build/host/network_profiles_test
./build/host/network_profiles_test

g++ "${CXXFLAGS[@]}" "${INCLUDES[@]}" \
  tests/host/camper_network_test.cpp VictronCYD_Modbus/CamperNetwork.cpp \
  -o build/host/camper_network_test
./build/host/camper_network_test

g++ "${CXXFLAGS[@]}" "${INCLUDES[@]}" \
  tests/host/touch_input_release_test.cpp VictronCYD_Modbus/TouchInput.cpp \
  -o build/host/touch_input_release_test
./build/host/touch_input_release_test

g++ "${CXXFLAGS[@]}" "${INCLUDES[@]}" \
  tests/host/credential_entry_controller_test.cpp VictronCYD_Modbus/CredentialEntryController.cpp \
  -o build/host/credential_entry_controller_test
./build/host/credential_entry_controller_test

g++ "${CXXFLAGS[@]}" "${INCLUDES[@]}" \
  tests/host/credential_keyboard_layout_test.cpp VictronCYD_Modbus/CredentialKeyboardLayout.cpp \
  -o build/host/credential_keyboard_layout_test
./build/host/credential_keyboard_layout_test

g++ "${CXXFLAGS[@]}" "${INCLUDES[@]}" \
  tests/host/wifi_setup_ui_test.cpp VictronCYD_Modbus/WifiSetupUi.cpp \
  VictronCYD_Modbus/CredentialEntryController.cpp \
  VictronCYD_Modbus/CredentialKeyboardLayout.cpp \
  -o build/host/wifi_setup_ui_test
./build/host/wifi_setup_ui_test

g++ "${CXXFLAGS[@]}" "${INCLUDES[@]}" \
  tests/host/provisioning_portal_test.cpp VictronCYD_Modbus/ProvisioningPortal.cpp \
  VictronCYD_Modbus/CredentialEntryController.cpp \
  -o build/host/provisioning_portal_test
./build/host/provisioning_portal_test

echo "host suites: 11/11 passed"
```

### Touch Wi-Fi setup

Selecting an unknown protected network opens the on-device keyboard. Passwords
are masked by default; use `Show` only when visual confirmation is needed.
`Connect` becomes available after a valid password is entered. If a connection
attempt fails, the keyboard returns with the password still masked so it can be
corrected. Select `Use phone` to clear the local entry and use the private
phone portal fallback instead.

## Firmware build

```bash
arduino-cli compile \
  --warnings all \
  --fqbn esp32:esp32:esp32 \
  --build-path build/linux-review \
  VictronCYD_Modbus
```

Before claiming a reproducible baseline, also run:

```bash
git diff --check
git status --short --branch
```

## Optional hardware upload

Do not upload merely to establish the Linux checkout: the bench board already
contains reviewed firmware `a6827e6`. Upload only when the board is connected to
Linux and a reviewed firmware change or explicit reflash is required.

```bash
arduino-cli board list

# Replace /dev/ttyUSB0 with the detected port.
arduino-cli upload \
  --port /dev/ttyUSB0 \
  --fqbn esp32:esp32:esp32 \
  --build-path build/linux-review \
  --verify \
  VictronCYD_Modbus
```

An ordinary upload must not erase NVS at `0x9000..0xDFFF`.
