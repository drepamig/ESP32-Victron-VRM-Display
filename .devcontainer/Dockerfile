FROM debian:bookworm-slim@sha256:88200866dfff7ea7f5cbcb6ec7c8a701889efe6fe859fe64d6990e4b07ea4171

ARG ARDUINO_CLI_VERSION=1.5.1
ARG ARDUINO_CLI_SHA256=28a8e119c498a25607821c36cb2dc49e8463941b261a0d99091baa7bc692dd2b
ARG WOKWI_CLI_VERSION=0.26.1
ARG WOKWI_CLI_SHA256=72d968150babaf7532c47ab7741eb85a95a6c3e3cfef91ffbcfdb677fd38e811
ARG PILLOW_VERSION=11.3.0

ENV DEBIAN_FRONTEND=noninteractive \
    ARDUINO_DIRECTORIES_DATA=/opt/arduino/data \
    ARDUINO_DIRECTORIES_DOWNLOADS=/opt/arduino/downloads \
    ARDUINO_DIRECTORIES_USER=/opt/arduino/user \
    ARDUINO_UPDATER_ENABLE_NOTIFICATION=false \
    PATH=/opt/bench-venv/bin:/usr/local/bin:/usr/bin:/bin

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash binutils build-essential ca-certificates curl git python3 python3-pip python3-venv \
    && rm -rf /var/lib/apt/lists/*

RUN set -eux; \
    archive=/tmp/arduino-cli.tar.gz; \
    curl --fail --location --retry 3 \
      "https://github.com/arduino/arduino-cli/releases/download/v${ARDUINO_CLI_VERSION}/arduino-cli_${ARDUINO_CLI_VERSION}_Linux_64bit.tar.gz" \
      --output "${archive}"; \
    echo "${ARDUINO_CLI_SHA256}  ${archive}" | sha256sum --check --strict; \
    tar -xzf "${archive}" -C /usr/local/bin arduino-cli; \
    rm "${archive}"; \
    arduino-cli version | grep -F "Version: ${ARDUINO_CLI_VERSION}"

RUN set -eux; \
    binary=/usr/local/bin/wokwi-cli; \
    curl --fail --location --retry 3 \
      "https://github.com/wokwi/wokwi-cli/releases/download/v${WOKWI_CLI_VERSION}/wokwi-cli-linuxstatic-x64" \
      --output "${binary}"; \
    echo "${WOKWI_CLI_SHA256}  ${binary}" | sha256sum --check --strict; \
    chmod 0755 "${binary}"; \
    wokwi-cli --version | grep -F "${WOKWI_CLI_VERSION}"

RUN python3 -m venv /opt/bench-venv \
    && pip install --no-cache-dir "Pillow==${PILLOW_VERSION}"

RUN mkdir -p "${ARDUINO_DIRECTORIES_DATA}" "${ARDUINO_DIRECTORIES_DOWNLOADS}" \
      "${ARDUINO_DIRECTORIES_USER}" \
    && arduino-cli core update-index \
       --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json \
    && arduino-cli core install esp32:esp32@3.3.11 \
       --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json \
    && arduino-cli lib update-index \
    && arduino-cli lib install "TFT_eSPI@2.5.43" \
    && arduino-cli lib install "XPT2046_Touchscreen@1.4" \
    && arduino-cli lib install "Adafruit FT6206 Library@1.1.1"

WORKDIR /workspace

CMD ["bash"]
