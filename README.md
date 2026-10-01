# MicroPixel

[English](README.md) | [简体中文](README.zh-CN.md)

> **This repository is a port of [MicroPixel](https://github.com/78/micropixel) to the
> Cheeko Gotchi** by ALTIO AI Private Limited. It tracks upstream and adds one board; see
> [Cheeko Gotchi](#cheeko-gotchi) below. Everything else is upstream MicroPixel.

[MicroPixel](https://micropixel.ai) runs WebAssembly apps on Espressif microcontrollers.
Apps use a C++23 SDK for graphics, input, audio, storage, and sensors, without depending on a board-specific SDK.
The firmware manages hardware, app isolation, and the system UI.

## Hardware

| Chip | Board |
|---|---|
| ESP32-P4 | [Metalio-Claw4](https://github.com/CloudZao/MetalioClaw4) |
| ESP32-S31 | [ESP-Mosaico](https://github.com/esp-mosaico/esp-mosaico-bsp) |
| ESP32-S3 | [ESP32-S3-BOX-3](https://github.com/espressif/esp-box) |
| ESP32-S3 | [LCKFB SZPI](https://wiki.lckfb.com/zh-hans/szpi-esp32s3/beginner/introduction.html) |
| ESP32-S3 | [M5Stack CoreS3](https://docs.m5stack.com/en/core/CoreS3) |
| ESP32-S3 | [SenseCAP Watcher](https://wiki.seeedstudio.com/cn/getting_started_with_watcher/) |
| ESP32-S3 | [Cheeko Gotchi](#cheeko-gotchi) (this fork) |

The Host uses ESP-IDF 6.1 and a pinned [WAMR fork](https://github.com/78/wasm-micro-runtime).
Apps are compiled to architecture-specific AOT v6 bundles. The ABI is still evolving.

## Build an app

Install the [SDK](https://micropixel.ai/docs/environment/) and connect a device running MicroPixel firmware.
App development does not require ESP-IDF.

```sh
micropixel init my-app --app-id com.example.my-app --title "My App"
cd my-app
micropixel --transport usb run
```

`run` builds, installs, starts the app, and follows its logs. Ctrl-C stops log streaming; the app keeps running.
See the [quickstart](guest/sdk/QUICKSTART.md) and [publishing guide](guest/sdk/PUBLISHING.md).

## Build the firmware

Initialize submodules, activate ESP-IDF 6.1, and configure WASI SDK and the matching MicroPixel WAMRC.
See the [build guide](docs/development/flashing.zh-CN.md) (Chinese) for toolchain setup and flashing.

```sh
git submodule update --init --recursive
python3 -m pip install -r requirements-dev.txt
bash tools/p4.sh build-host
```

Other profiles: `bash tools/s31.sh build-host` and `bash tools/s3.sh build-host <box3|szpi|cores3|watcher|gotchi>`.

## Cheeko Gotchi

The Cheeko Gotchi (board revision OSTB_XIAOZHI_V1.2) is an ESP32-S3R8 with 16 MB flash, 8 MB octal PSRAM and
native USB. The board profile lives in
[`firmware/espressif/main/platform/boards/cheeko-gotchi`](firmware/espressif/main/platform/boards/cheeko-gotchi).

| Part | Support |
|---|---|
| JD9853 2.01" 240x296 IPS, SPI 40 MHz | Shown as 296x240 landscape with its own Host UI profile (`landscape_296`) |
| CST810 touch, I2C 0x15 | Polled |
| ES8311 + NS4150B speaker | Audio output |
| SC7A20 / LIS2DH12 accelerometer, I2C 0x19 | Acceleration sensor |
| BOOT, VOL-, VOL+ keys | Confirm, Left, Right |
| Native Wi-Fi, USB Serial/JTAG | Local control, screenshots, logs |
| ES7210 microphones, battery level, power key | Not supported yet |

The Host firmware needs only ESP-IDF 6.1 (the commit pinned in `tools/ci/firmware-sources.json`). WASI SDK and
the Xtensa WAMRC are only needed to build Apps yourself: the stock S3 Apps can be taken from an official
release instead, because every 16 MB ESP32-S3 board shares the same App Store.

```sh
git submodule update --init --recursive
source /path/to/esp-idf-v6.1/export.sh
python3 -m pip install -r requirements-dev.txt
bash tools/s3.sh build-host gotchi
python3 tools/fetch_release_app_store.py
bash tools/s3.sh flash-host gotchi /dev/cu.usbmodemXXXX
bash tools/s3.sh flash-apps gotchi /dev/cu.usbmodemXXXX
```

The build directory must not contain spaces; an ESP-IDF component's patch step fails on such paths.

Status: the firmware boots on the board, the App Hall renders at 296x240 and the stock Apps run. Display,
colour order and touch are confirmed on the device. The accelerometer is mounted on the back of the board, turned
for the portrait panel; `sensor_peripheral.cpp` rotates it into the app convention (X right, Y up, Z out of the
screen), measured on the device.

## Project

- [Guest SDK](guest/sdk/README.md) — app APIs and examples.
- [Firmware](firmware/README.md) — Host runtime and board support.
- [Documentation](docs/README.md) — architecture, protocols, and development guides.
- [Contributing](CONTRIBUTING.md).

## License

Project-authored code, documentation, and assets use [Apache-2.0](LICENSE), unless stated otherwise.
See [third-party notices](THIRD_PARTY_NOTICES.md) for dependency licenses and exceptions.
