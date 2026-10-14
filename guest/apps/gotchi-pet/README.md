# Gotchi Pet

A desk pet face for the CGotchi, as a plain MicroPixel WebAssembly app (no firmware changes).

| You do | The pet does |
|---|---|
| Leave it alone | Blinks, glances around, eyes follow the tilt of the device |
| Tap the screen | Smiles with pink cheeks and chirps (+ joy) |
| VOL+ | Eats with a munching mouth (+ food) |
| VOL- | Bounces and plays (+ joy, a little food) |
| Shake it | Spinning dizzy eyes (- joy) |
| Ignore it 45 s / 60 s | Gets sleepy, then sleeps with "z Z z"; any tap, key or nudge wakes it |
| Let food drop below 25 | Orange droopy eyes, "I'm hungry! Press VOL+" |

Food (orange bar) and joy (pink bar) drain only while the app is open (food lasts about an hour) and are
saved on the device between runs.

## Install on the Gotchi (Mac)
The Gotchi must be running MicroPixel firmware (not Muse).

```sh
cp -R gotchi-pet ~/Cgotchi/micropixel-gotchi/guest/apps/
cd ~/Cgotchi/micropixel-gotchi/guest/apps/gotchi-pet
micropixel --transport usb run
```

Same command you used for WisprKey. App ID `cheeko.gotchi-pet`.
