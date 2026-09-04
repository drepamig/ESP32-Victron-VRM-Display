#!/usr/bin/env bash
set -euo pipefail

mode="${1:-}"
if [[ "$mode" != "production" && "$mode" != "simulation" && "$mode" != "velxio" ]]; then
  echo "usage: tools/arduino-build.sh <production|simulation|velxio>" >&2
  exit 2
fi

stage_path="$(python3 tools/sim_artifacts.py stage --mode "$mode")"
build_path="build/arduino/$mode"
fqbn="esp32:esp32:esp32"
if [[ "$mode" == "velxio" ]]; then
  fqbn="esp32:esp32:esp32:FlashMode=dio"
fi
if [[ "$mode" == "production" ]]; then
  output_path="build/firmware"
  extra_flags="-include /workspace/build/staging/production/config/TFT_eSPI_CYD.h"
else
  output_path="build/$mode"
  extra_flags="-DCYD_SIMULATION -include /workspace/build/staging/$mode/config/TFT_eSPI_CYD.h"
fi
mkdir -p "$build_path" "$output_path"

arduino-cli compile \
  --clean \
  --warnings all \
  --fqbn "$fqbn" \
  --build-path "$build_path" \
  --output-dir "$output_path" \
  --build-property "compiler.c.extra_flags=$extra_flags" \
  --build-property "compiler.cpp.extra_flags=$extra_flags" \
  "$stage_path"

if [[ "$mode" != "production" ]]; then
  cp "$output_path/VictronCYD_Modbus.ino.merged.bin" "$output_path/firmware.bin"
  cp "$output_path/VictronCYD_Modbus.ino.elf" "$output_path/firmware.elf"
  python3 tools/sim_artifacts.py attest --mode "$mode"
  python3 tools/sim_artifacts.py verify --mode "$mode"
else
  cp /opt/arduino/data/packages/esp32/hardware/esp32/3.3.11/tools/partitions/boot_app0.bin "$output_path/boot_app0.bin"
fi
