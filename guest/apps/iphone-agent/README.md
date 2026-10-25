# iPhone Agent

Based on [ESP32 iPhone Agent](https://github.com/onehumanbeing/esp32-iphone-agent) by Henry Yuan
(MIT, `Copyright (c) 2026 Henry Yuan`), ported to a Cheeko Gotchi App. Public demos, videos and
posts of this App should credit that project too.

The iPhone mirrors its screen to the Gotchi over AirPlay. The Gotchi forwards the H.264 stream to
the **ESP32 Agent** iPhone app over Bluetooth LE and is the phone's Bluetooth mouse and keyboard
(AssistiveTouch shows the pointer). The phone app holds all the logic and talks the original
firmware's protocol unchanged.

## What it needs from the Host

Host firmware with the services added for this App (branch `esp32-agent`):

- **HID keyboard 1.1**: a relative mouse report next to the keyboard, and FORGET_BONDS.
- **BLE Link 1.0**: the original firmware's GATT service (`6e400001-a9e8-4b1b-9c39-6d6f8a3c0000`) on
  the keyboard's connection, with its send pacing.
- **Network 1.0**: non-blocking TCP/UDP sockets, mDNS adverts and a WPA2 hotspot.

## Using it

1. iPhone: Settings > Bluetooth > pair **CGotchi**. Settings > Accessibility > Touch >
   AssistiveTouch on (the Gotchi is the pointing device).
2. Open **iPhone Agent** on the Gotchi; keep it on screen while the agent runs.
3. Wi-Fi (VOL- / VOL+ on the Gotchi):
   - **Auto**: uses the Wi-Fi the Gotchi already joined (home, office); the iPhone joins the same
     network and keeps its internet. With no saved network in range for 10 s it opens the hotspot.
   - **Wi-Fi only** / **Hotspot only**. The hotspot name and password are on the Gotchi's screen
     (and the phone app reads them over Bluetooth); the iPhone uses cellular data for the model.
4. iPhone: Control Center > Screen Mirroring > **ESP32 Agent**, then run tasks from the phone app.

## Differences from the USB board

- Mouse and keyboard go over Bluetooth instead of USB: one report per ~15 ms connection
  interval. The phone app calibrates the pointer itself; re-run its calibration once.
- The board reports `usb: ok` in its status when the Bluetooth mouse is paired and encrypted.
- `coex` always answers `balance` (the Host owns the radio share).

## Licenses

The AirPlay receiver in `airplay/` is ported from [UxPlay](https://github.com/FDH2/UxPlay) and is
GPL-3.0 (`airplay/LICENSE-GPL.md`), so this App as a whole is distributed under GPL-3.0; the Host
firmware it runs on is separate and keeps its own license. Also included: Monocypher
(`vendor/monocypher`, CC0 / BSD-2), tiny-AES-c (`vendor/tiny-aes`, Unlicense) and playfair
(`airplay/playfair/LICENSE.md`).
