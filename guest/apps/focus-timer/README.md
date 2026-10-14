# Focus Timer

A Pomodoro timer with the Gotchi Pet face, as a plain MicroPixel WebAssembly app (no firmware changes).

| You do | It does |
|---|---|
| VOL- / VOL+ (idle) | Pick focus length: 15, 25, 45 or 60 min (remembered) |
| Tap | Start focus; eyes narrow and look down at your work |
| Tap while running | Pause / resume (eyes go grey). VOL- while paused resets |
| Session ends | Chime, happy green eyes, "Tap for a break" |
| Ignore the break 60 s | Red eyes with frowning lids, ">:(", a grumble every 30 s until you take it |
| Break | 5 min, every 4th break is 15 min. Tomato dots count the 4 sessions |

## Install on the Gotchi (Mac)
```sh
cp -R focus-timer ~/Cgotchi/micropixel-gotchi/guest/apps/
cd ~/Cgotchi/micropixel-gotchi/guest/apps/focus-timer
micropixel --transport usb run
```
App ID `cheeko.focus-timer`.
