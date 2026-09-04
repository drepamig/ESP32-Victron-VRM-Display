#!/usr/bin/env bash
set -euo pipefail

production_elf="build/firmware/VictronCYD_Modbus.ino.elf"
mode="${2:-}"
if [[ $# -gt 0 && ( "$1" != "--mode" || ( "$mode" != "simulation" && "$mode" != "velxio" ) || $# -ne 2 ) ]]; then
  echo "usage: tools/check-isolation.sh [--mode simulation|velxio]" >&2
  exit 2
fi
modes=(simulation velxio)
if [[ -n "$mode" ]]; then modes=("$mode"); fi

[[ -f "$production_elf" ]] || {
  echo "production ELF is missing; run firmware-build first" >&2
  exit 1
}
for mode in "${modes[@]}"; do
  simulation_stage="build/staging/$mode"
  [[ -d "$simulation_stage" && -d "build/arduino/$mode" ]] || {
    echo "$mode staging or build dependencies are missing; build this backend first" >&2
    exit 1
  }

  if find "$simulation_stage" -type f -iname 'secrets.h' -print -quit | grep -q .; then
    echo "$mode stage contains a secrets.h file" >&2
    exit 1
  fi

  if grep -R -I -F -e '/workspace/VictronCYD_Modbus/secrets.h' \
                        -e '/workspace/VictronCYD/secrets.h' \
                        "$simulation_stage" "build/arduino/$mode"; then
    echo "$mode staging or dependencies reference production secret paths" >&2
    exit 1
  fi
  python3 tools/sim_artifacts.py verify --mode "$mode"
done

if strings "$production_elf" | grep -E \
    'Simulation(Control|Clock)|SimCamperNetwork|SimModbusCycleSource|CYD_SIMULATION_CONFIG_ID|cyd-sim-dummy-v1|SIM (clock|scan|connect|modbus|wan|reset)'; then
  echo "production firmware contains simulator symbols or configuration" >&2
  exit 1
fi

echo "production/simulator isolation: verified"
