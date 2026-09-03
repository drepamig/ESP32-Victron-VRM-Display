#!/usr/bin/env bash
set -euo pipefail

mode="${1:-}"
if [[ "$mode" != "production" && "$mode" != "simulation" ]]; then
  echo "usage: tools/arduino-build.sh <production|simulation>" >&2
  exit 2
fi

stage_path="$(python3 tools/sim_artifacts.py stage --mode "$mode")"
build_path="build/arduino/$mode"
if [[ "$mode" == "production" ]]; then
  output_path="build/firmware"
  extra_flags="-include /workspace/config/TFT_eSPI_CYD.h"
else
  output_path="build/simulation"
  extra_flags="-DCYD_SIMULATION -include /workspace/config/TFT_eSPI_CYD.h"
fi
mkdir -p "$build_path" "$output_path"

arduino-cli compile \
  --clean \
  --warnings all \
  --fqbn esp32:esp32:esp32 \
  --build-path "$build_path" \
  --output-dir "$output_path" \
  --build-property "compiler.c.extra_flags=$extra_flags" \
  --build-property "compiler.cpp.extra_flags=$extra_flags" \
  "$stage_path"

if [[ "$mode" == "simulation" ]]; then
  cp "$output_path/VictronCYD_Modbus.ino.merged.bin" "$output_path/firmware.bin"
  cp "$output_path/VictronCYD_Modbus.ino.elf" "$output_path/firmware.elf"
  python3 tools/sim_artifacts.py attest
  python3 tools/sim_artifacts.py verify
fi
