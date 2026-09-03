#!/usr/bin/env bash
set -euo pipefail

command_name="${1:-doctor}"
shift || true

case "$command_name" in
  doctor)
    arduino-cli version
    wokwi-cli --version
    g++ --version | head -n 1
    python3 --version
    python3 -c 'from PIL import __version__; print("Pillow", __version__)'
    arduino-cli core list | grep -F 'esp32:esp32'
    arduino-cli lib list | grep -E '^(TFT_eSPI|XPT2046_Touchscreen|Adafruit FT6206 Library)[[:space:]]'
    ;;
  test)
    bash tools/run-host-tests.sh
    python3 -m unittest discover -s tests/tools -p 'test_*.py' -v
    ;;
  firmware-build)
    bash tools/arduino-build.sh production
    ;;
  sim-build)
    bash tools/arduino-build.sh simulation
    ;;
  sim-test)
    bash tools/arduino-build.sh simulation
    python3 tools/run_wokwi.py test "$@"
    ;;
  sim-update-goldens)
    bash tools/arduino-build.sh simulation
    python3 tools/run_wokwi.py update-goldens "$@"
    ;;
  all)
    bash tools/run-host-tests.sh
    python3 -m unittest discover -s tests/tools -p 'test_*.py' -v
    bash tools/arduino-build.sh production
    bash tools/arduino-build.sh simulation
    bash tools/check-isolation.sh
    python3 tools/run_wokwi.py test "$@"
    ;;
  *)
    echo "Unknown bench command: $command_name" >&2
    exit 2
    ;;
esac
