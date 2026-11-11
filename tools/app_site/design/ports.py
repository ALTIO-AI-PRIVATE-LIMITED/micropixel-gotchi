#!/usr/bin/env python3
"""Store listings for the apps ported to the Gotchi: text, credit and cover art.

python3 design/ports.py <dir-with-built-bundles>  adds covers, app_info.json
entries and index.json entries for every port that has a bundle there.
"""
import hashlib, json, shutil, sys
from pathlib import Path
import covers
from covers import gotchi, frame

SITE = Path(__file__).resolve().parents[1]
REPO_TREE = "https://github.com/ALTIO-AI-PRIVATE-LIMITED/micropixel-gotchi/tree/app-manager/guest/apps/"

P = {}
def port(name, title, kind, by, repo, license, summary, about, needs, bg, art, tag):
    P[name] = dict(title=title, kind=kind, by=by, repo="https://github.com/" + repo, license=license,
                   summary=summary, about=about, needs=needs, bg=bg, art=art, tag=tag)

bars = "".join(f'<rect x="{10+i*18}" y="{220-h}" width="14" height="{h}" fill="{c}"/>' for i, (h, c) in enumerate(
    [(60,"#2ad196"),(110,"#2ad196"),(170,"#5fd080"),(190,"#ffb020"),(150,"#ff8a3d"),(120,"#ff8a3d"),(160,"#ff5a50"),(90,"#ff5a50"),(70,"#c44"),(120,"#ff5a50"),(60,"#2ad196"),(90,"#2ad196"),(40,"#2ad196"),(60,"#2ad196"),(30,"#2ad196"),(20,"#2ad196")]))
cells = "".join(f'<rect x="{(i*37)%280+8}" y="{(i*53)%220+8}" width="14" height="14" fill="#3ee6c4"/>' for i in range(46))
reel = lambda x, s: f'<rect x="{x}" y="40" width="82" height="160" fill="#f5f2e8"/>' + s
seven = lambda x: f'<text x="{x}" y="145" text-anchor="middle" font-family="Arial Black,Arial" font-weight="900" font-size="74" fill="#e2363e">7</text>'
frac = '<rect width="296" height="240" fill="#081040"/><circle cx="170" cy="120" r="62" fill="#000"/><circle cx="92" cy="120" r="30" fill="#000"/><circle cx="54" cy="120" r="10" fill="#000"/>' + "".join(
    f'<circle cx="170" cy="120" r="{r}" fill="none" stroke="{c}" stroke-width="7"/>' for r, c in [(70,"#1e78dc"),(84,"#fae696"),(98,"#be3c14")])
glass = '<path d="M90 24 H206 V70 L156 118 L206 170 V216 H90 V170 L140 118 L90 70 Z" fill="none" stroke="#78aadc" stroke-width="6"/><path d="M104 62 H192 L150 112 Z" fill="#ffc450"/><path d="M148 122 v44" stroke="#ffc450" stroke-width="5"/><path d="M98 208 Q148 150 198 208 Z" fill="#f4a03c"/>'
wave = "".join(f'<rect x="{24+i*8}" y="{120-h}" width="5" height="{h*2}" fill="#6490e6"/>' for i, h in enumerate([8,22,40,14,30,60,44,20,12,36,70,50,24,10,34,16,48,30,12,8,26,14,6,4,2,10,18,6]))

port("port-cardlife", "CardLife", "Simulation", "Mystereon", "Mystereon/CardLife", "CC0",
     "A living cellular world that grows, dies and evolves on your screen.",
     "A cellular automaton with the original rules: a cell survives with 2 or 3 neighbours and is born with 3 or 7. Tap to nuke the world and reseed it, press BOOT to change the cell size.",
     ["Just a Gotchi"], ("#0b3d36", "#061a18"), cells, "Life rules on a 296x240 world")
port("port-slot-machine", "Slot Machine", "Game", "Sarah-C", "Sarah-C/M5StickC_Plus_Slot_Machine", "MIT",
     "A three reel slot machine. Tap to spin, tap to stop each reel.",
     "Same reel order and spin physics as the original, with the symbols redrawn. Credits are play money only and refill when you run out. There is nothing to buy and nothing to win.",
     ["Just a Gotchi", "Speaker for the sound effects"], ("#4a1a6b", "#1a0f2e"),
     reel(6, seven(47)) + reel(107, seven(148)) + reel(208, seven(249)), "Three reels, play money only")
port("port-mandelbrot", "Mandelbrot", "Art", "mbogatyr", "mbogatyr/M5Fractal", "MIT",
     "Tap anywhere to dive into the Mandelbrot set.",
     "Each tap zooms in by two on that point. The picture appears in two passes, coarse then fine. VOL+ zooms out, VOL- changes the palette, BOOT resets the view. Drawn in 4 pixel blocks, so it is chunkier than the original.",
     ["Just a Gotchi"], ("#0f1f6b", "#050a2a"), frac, "Tap to zoom into infinity")
port("port-spectrum", "Spectrum Analyzer", "Audio", "mbogatyr", "mbogatyr/M5SpectrumAnalyzer", "MIT",
     "See the sound around you as live bars from 100 Hz to 8 kHz.",
     "Listens to the microphone, runs a 512 point FFT and shows 32 logarithmic bands with falling peak marks. Tap or press a key to change the colors. The audio is never stored or sent anywhere.",
     ["Uses the Gotchi microphone"], ("#143a2c", "#06140f"), bars, "Your room, as live bars")
port("port-hourglass", "Liquid Hourglass", "Toy", "oliverxing2025", "oliverxing2025/VibeStick-Hourglass-Liquid", "MIT",
     "A sand timer with real particle physics. Tilt it and the sand follows.",
     "Uses the original 300 particle simulation unchanged. Tap to restart, BOOT to switch between 1, 0.5, 3 and 5 minutes. Turn the Gotchi upside down to flip the glass.",
     ["Uses the Gotchi accelerometer"], ("#5a3a10", "#1c1206"), glass, "300 grains of tilting sand")
port("port-voice-recorder", "Voice Recorder", "Audio", "mbogatyr", "mbogatyr/M5VoiceRecorder", "MIT",
     "Record a 20 second voice note and play it back.",
     "Records one take at 16 kHz using the original ADPCM codec. The original saves WAV files, but Gotchi apps have no file storage, so this take lives in memory and is gone when you close the app.",
     ["Uses the Gotchi microphone and speaker"], ("#5b1a24", "#1c080c"), wave, "Quick notes, kept until you exit")

def main(built):
    built = Path(built)
    info_path, index_path = SITE / "app_info.json", SITE / "private/apps/index.json"
    info, index = json.loads(info_path.read_text()), json.loads(index_path.read_text())
    apps = {a["appId"]: a for a in index["apps"]}
    covers.COVERS.clear()
    for folder, p in P.items():
        bundle = built / f"{folder}.bin"
        if not bundle.is_file():
            continue
        manifest = json.loads((SITE.parents[1] / "guest/apps" / folder / "app.json").read_text())
        app_id = manifest["app_id"]
        target = SITE / "private/apps" / f"{folder}.bundle.bin"
        shutil.copyfile(bundle, target)
        data = target.read_bytes()
        apps[app_id] = {"appId": app_id, "title": p["title"], "version": manifest["version"], "summary": "",
                        "requirements": manifest["requirements"], "target": "xtensa", "file": f"apps/{target.name}",
                        "sizeBytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
        info[app_id] = {"summary": p["summary"], "about": p["about"], "why": f"Ported to the Gotchi from {p['by']}'s open source project.",
                        "needs": p["needs"], "kind": p["kind"], "cover": f"covers/{app_id}.png",
                        "credit": {"by": p["by"], "repo": p["repo"], "license": p["license"], "source": REPO_TREE + folder}}
        covers.COVERS[app_id] = frame(p["bg"][0], p["bg"][1], gotchi(330, 30, 250, p["art"]), p["title"], p["tag"])
    index["apps"] = list(apps.values())
    index_path.write_text(json.dumps(index, indent=2, ensure_ascii=False))
    entries = (f'  {json.dumps(k)}: {{\n' + ",\n".join(f'    {json.dumps(a)}: {json.dumps(b, ensure_ascii=False)}' for a, b in v.items()) + "\n  }" for k, v in info.items())
    info_path.write_text("{\n" + ",\n".join(entries) + "\n}\n")
    covers.main()
    print(len(index["apps"]), "apps in the store")

if __name__ == "__main__":
    main(sys.argv[1])
