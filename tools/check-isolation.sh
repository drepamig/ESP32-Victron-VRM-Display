#!/usr/bin/env bash
set -euo pipefail

production_elf="build/firmware/VictronCYD_Modbus.ino.elf"
simulation_stage="build/staging/simulation"

[[ -f "$production_elf" ]] || {
  echo "production ELF is missing; run firmware-build first" >&2
  exit 1
}
[[ -d "$simulation_stage" ]] || {
  echo "simulation stage is missing; run sim-build first" >&2
  exit 1
}

if find "$simulation_stage" -type f -iname 'secrets.h' -print -quit | grep -q .; then
  echo "simulation stage contains a secrets.h file" >&2
  exit 1
fi

if grep -R -I -F -e '/workspace/VictronCYD_Modbus/secrets.h' \
                      -e '/workspace/VictronCYD/secrets.h' \
                      "$simulation_stage" build/arduino/simulation; then
  echo "simulator staging or dependencies reference production secret paths" >&2
  exit 1
fi

if strings "$production_elf" | grep -E \
    'Simulation(Control|Clock)|SimCamperNetwork|SimModbusCycleSource|CYD_SIMULATION_CONFIG_ID|cyd-sim-dummy-v1|SIM (clock|scan|connect|modbus|reset)'; then
  echo "production firmware contains simulator symbols or configuration" >&2
  exit 1
fi

python3 tools/sim_artifacts.py verify
echo "production/simulator isolation: verified"
