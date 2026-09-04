#!/usr/bin/env bash
set -euo pipefail

output_dir="build/host"
mkdir -p "$output_dir"

common=(-std=c++17 -Wall -Wextra -Werror -Itests/host/fakes -IVictronCYD_Modbus)

build_and_run() {
  local name="$1"
  shift
  g++ "${common[@]}" "$@" -o "$output_dir/$name"
  "$output_dir/$name"
}

build_and_run gateway_policy_test tests/host/gateway_policy_test.cpp
build_and_run gateway_application_policy_test tests/host/gateway_application_policy_test.cpp
build_and_run time_settings_test -Wl,--wrap=setenv tests/host/time_settings_test.cpp VictronCYD_Modbus/TimeSettings.cpp VictronCYD_Modbus/TimeZoneCatalog.cpp
build_and_run settings_ui_test tests/host/settings_ui_test.cpp VictronCYD_Modbus/SettingsUi.cpp VictronCYD_Modbus/TimeSettings.cpp VictronCYD_Modbus/TimeZoneCatalog.cpp
build_and_run modbus_snapshot_policy_test tests/host/modbus_snapshot_policy_test.cpp
build_and_run touch_mapping_test tests/host/touch_mapping_test.cpp
build_and_run network_profiles_test tests/host/network_profiles_test.cpp VictronCYD_Modbus/NetworkProfiles.cpp
build_and_run ipv4_bridge_core_test tests/host/ipv4_bridge_core_test.cpp VictronCYD_Modbus/Ipv4BridgeCore.cpp
build_and_run ipv4_bridge_adapter_test -DCYD_BRIDGE_HOST_TEST tests/host/ipv4_bridge_adapter_test.cpp VictronCYD_Modbus/Ipv4Bridge.cpp VictronCYD_Modbus/Ipv4BridgeCore.cpp
build_and_run venus_address_tracker_test tests/host/venus_address_tracker_test.cpp VictronCYD_Modbus/VenusAddressTracker.cpp
g++ -Itests/host/transport_fakes "${common[@]}" tests/host/tcp_modbus_target_test.cpp VictronCYD_Modbus/TcpModbusCycleSource.cpp -o "$output_dir/tcp_modbus_target_test"
"$output_dir/tcp_modbus_target_test"
build_and_run camper_network_test tests/host/camper_network_test.cpp VictronCYD_Modbus/CamperNetwork.cpp tests/host/fakes/Ipv4BridgeFake.cpp
build_and_run gateway_connection_controller_test tests/host/gateway_connection_controller_test.cpp VictronCYD_Modbus/CamperNetwork.cpp tests/host/fakes/Ipv4BridgeFake.cpp VictronCYD_Modbus/NetworkProfiles.cpp VictronCYD_Modbus/WifiSetupUi.cpp VictronCYD_Modbus/CredentialEntryController.cpp VictronCYD_Modbus/CredentialKeyboardLayout.cpp
build_and_run touch_input_release_test tests/host/touch_input_release_test.cpp VictronCYD_Modbus/TouchInput.cpp VictronCYD_Modbus/RawTouchDevice.cpp
build_and_run credential_entry_controller_test tests/host/credential_entry_controller_test.cpp VictronCYD_Modbus/CredentialEntryController.cpp
build_and_run credential_keyboard_layout_test tests/host/credential_keyboard_layout_test.cpp VictronCYD_Modbus/CredentialKeyboardLayout.cpp
build_and_run wifi_setup_ui_test tests/host/wifi_setup_ui_test.cpp VictronCYD_Modbus/WifiSetupUi.cpp VictronCYD_Modbus/CredentialEntryController.cpp VictronCYD_Modbus/CredentialKeyboardLayout.cpp
build_and_run provisioning_portal_test tests/host/provisioning_portal_test.cpp VictronCYD_Modbus/ProvisioningPortal.cpp VictronCYD_Modbus/CredentialEntryController.cpp
build_and_run ft6206_raw_touch_device_test -DCYD_SIMULATION tests/host/ft6206_raw_touch_device_test.cpp VictronCYD_Modbus/RawTouchDevice.cpp

if [[ -f tests/host/raw_touch_device_test.cpp ]]; then
  build_and_run raw_touch_device_test -DCYD_HOST_TEST tests/host/raw_touch_device_test.cpp VictronCYD_Modbus/RawTouchDevice.cpp
fi
if [[ -f tests/host/sim_camper_network_test.cpp ]]; then
  build_and_run sim_camper_network_test -DCYD_SIMULATION tests/host/sim_camper_network_test.cpp VictronCYD_Modbus/SimCamperNetwork.cpp
fi
if [[ -f tests/host/modbus_cycle_source_test.cpp ]]; then
  build_and_run modbus_cycle_source_test -DCYD_SIMULATION tests/host/modbus_cycle_source_test.cpp VictronCYD_Modbus/SimModbusCycleSource.cpp
fi
if [[ -f tests/host/simulation_control_test.cpp ]]; then
  build_and_run simulation_control_test -DCYD_SIMULATION tests/host/simulation_control_test.cpp VictronCYD_Modbus/SimulationControl.cpp VictronCYD_Modbus/SimulationClock.cpp VictronCYD_Modbus/SimCamperNetwork.cpp VictronCYD_Modbus/SimModbusCycleSource.cpp
fi

echo "host suites: all passed"
