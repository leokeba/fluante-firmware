# Fluante

Firmware for a Fluante: a 3D-printed sculpture turned by a stepper motor and lit by a strobe. It runs on a classic ESP32 with a TMC2209, on [TesserKIT](https://github.com/leokeba/TesserKIT) (Wi-Fi, updates, the interface, the Fluantes nearby over ESP-NOW) and [TesserMotion](https://github.com/leokeba/TesserMotion) (the motor driver). It replaces [fluante_firmware](https://github.com/leokeba/fluante_firmware).

[`docs/DESIGN.md`](docs/DESIGN.md) is the specification.

## Use

Flash it, join the `fluante-xxxx` network from a phone, and the interface opens. A fresh Fluante has its motor off and its strobe on, at 60.01 Hz:

1. Set the motor's current (Motor driver → Driver settings → runCurrent; 0.95 A by default).
2. Set the speed (Rotation → speed, rpm of the sculpture) and turn Rotation → running on. While on, it turns again at every power-up.
3. Set the strobe's frequency and duty (Strobe).

Other Fluantes nearby show on the first page and under `/peers`.

## Install and update

The [launcher](https://leokeba.github.io/fluante-firmware/) reaches a Fluante by its address or over USB, and installs the firmware on a new board over USB (Chrome or Edge on a computer), then sets up its Wi-Fi. On a Mac, the bridge in each [release](https://github.com/leokeba/fluante-firmware/releases/latest) (`fluante-bridge-macos-arm64.zip` for Apple silicon, `-x64.zip` for Intel) lists the Fluantes on the network and serves the same launcher on localhost, in any browser. It isn't notarized: macOS asks once to open it (*System Settings*, *Privacy & Security*, *Open Anyway*). Without either, `fluante-esp32-factory.bin` goes at offset 0 over USB.

Each push to `main` publishes a release. A Fluante online checks for a new one daily and shows it under System → Firmware (`/sys/firmware/github`), where `update` installs it over the air; an image that doesn't confirm itself rolls back.

## Build

The libraries are checkouts next to this repository, at the commits [`deps.lock`](deps.lock) pins (`scripts/pin.sh` updates it): TesserAPI, TesserUI, NowTP, TesserKIT, TesserMotion.

```sh
cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure   # host tests
. ~/esp/esp-idf/export.sh
idf.py -C firmware -B /tmp/fluante-idf set-target esp32 build flash   # the firmware
```

A gitignored `firmware/main/secrets.h` may define `WIFI_SSID` and `WIFI_PASSWORD` for development.
