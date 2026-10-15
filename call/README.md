# Gotchi Call

Press the middle button on a CGotchi and your iPhone rings. Answer it and you talk
through the Gotchi over LiveKit (WebRTC).

```
Gotchi --POST /api/call--> call server --VoIP push--> iPhone (CallKit rings)
   |                           | creates room, mints both tokens       | answer
   +------ joins room <--------+------------- LiveKit -----------------+ joins room
```

| Folder | What |
|---|---|
| `firmware/` | Standalone ESP-IDF firmware for the Gotchi (replaces MicroPixel while you test it) |
| `server/` | Call server: Vercel functions that create the room, mint tokens and send the push |
| `ios/` | `GotchiCallManager.swift`, a PushKit + CallKit + LiveKit drop-in for the iPhone app |

## Gotchi controls

- **Middle button**: call; while ringing, cancel; during a call, hang up. Hold for 2 s to power off.
- **Vol+ / Vol-**: speaker volume.

## 1. Call server

Deploy `server/` to Vercel (`vercel deploy` from that folder) and set these environment variables:

| Variable | Value |
|---|---|
| `LIVEKIT_URL` | Your LiveKit URL, e.g. `wss://your-project.livekit.cloud` |
| `LIVEKIT_API_KEY`, `LIVEKIT_API_SECRET` | From the LiveKit project settings |
| `DEVICE_SECRET` | Any long random string; the Gotchi sends it |
| `APP_SECRET` | Another long random string; the iPhone app sends it |
| `APNS_KEY_ID`, `APNS_TEAM_ID` | Apple push key (.p8) ID and your team ID |
| `APNS_KEY` | Contents of the .p8 file |
| `APNS_BUNDLE_ID` | The iPhone app's bundle ID (the VoIP topic is this plus `.voip`) |
| `APNS_SANDBOX` | `1` for an app run from Xcode, unset for TestFlight/App Store |
| `IPHONE_VOIP_TOKEN` | The token the app prints as `Gotchi VoIP push token:` |

Endpoints: `POST /api/call` and `POST /api/call/end` (Gotchi, `Bearer DEVICE_SECRET`),
`POST /api/call/decline` (iPhone, `Bearer APP_SECRET`).

## 2. iPhone app

Add the LiveKit Swift SDK (2.x) and `ios/GotchiCallManager.swift`. Turn on Push Notifications and the
Background Modes "Voice over IP" and "Audio", add `NSMicrophoneUsageDescription`, set `serverURL` and
`appSecret` in the file, and call `GotchiCallManager.shared.start()` at launch.

## 3. Gotchi firmware

Needs ESP-IDF 5.4 or 5.5, the versions LiveKit's ESP32 SDK is tested on.

```sh
cd call/firmware
cp secrets.defaults.example secrets.defaults   # Wi-Fi, server URL, DEVICE_SECRET
./build.sh build flash monitor
```

This overwrites MicroPixel. To go back, flash your MicroPixel backup or rebuild MicroPixel.

The screen font is generated: `python3 tools/make_font.py <a .ttf> > main/font.h`.
