#!/usr/bin/env python3
"""Store listings for the apps written from scratch for the Gotchi.

python3 design/originals.py manifests            writes app.json for every listed app
python3 design/originals.py publish <built> <shots>  adds bundles, listings and covers
"""
import base64, hashlib, json, shutil, subprocess, sys, tempfile
from pathlib import Path
import covers
from covers import gotchi, frame

SITE = Path(__file__).resolve().parents[1]
APPS = SITE.parents[1] / "guest/apps"
REPO = "https://github.com/ALTIO-AI-PRIVATE-LIMITED/micropixel-gotchi"
TREE = REPO + "/tree/app-manager/guest/apps/"
TOUCH, SPK, KEYS, TILT = "input.touch", "audio.output", "input.keys", "sensor.acceleration"
GOTCHI = "Just a Gotchi"
MIC = "Uses the Gotchi microphone"
ACC = "Uses the Gotchi accelerometer"
SOUND = "Speaker for the sound"
FOREVER = "Keeps your best score on the Gotchi"

A = {}
def app(folder, title, kind, tag, summary, about, needs, bg, tilt=False, shot=600):
    A[folder] = dict(title=title, kind=kind, tag=tag, summary=summary, about=about, needs=needs, bg=bg, tilt=tilt, shot=shot)

exec((Path(__file__).parent / "originals_list.py").read_text())

def manifests():
    for folder, a in A.items():
        optional = [SPK, KEYS] + ([TILT] if a["tilt"] else [])
        d = {"schema_version": 1, "app_id": "cheeko." + folder, "version": "0.1.0", "threading": "none", "title": a["title"], "sources": ["main.cpp"],
             "requirements": {"schema_version": 1, "display": {"layouts": ["square", "portrait", "landscape"], "min_width": 240, "min_height": 240},
                              "required": [TOUCH], "optional": optional, "any_of": [], "services": {}}}
        (APPS / folder / "app.json").write_text(json.dumps(d, indent=2) + "\n")

def publish(built, shots):
    built, shots = Path(built), Path(shots)
    info_path, index_path = SITE / "app_info.json", SITE / "private/apps/index.json"
    info, index = json.loads(info_path.read_text()), json.loads(index_path.read_text())
    apps = {x["appId"]: x for x in index["apps"]}
    covers.COVERS.clear()
    tmp = Path(tempfile.mkdtemp())
    for folder, a in A.items():
        bundle = built / f"{folder}.bin"
        if not bundle.is_file():
            continue
        manifest = json.loads((APPS / folder / "app.json").read_text())
        app_id = manifest["app_id"]
        target = SITE / "private/apps" / f"{folder}.bundle.bin"
        shutil.copyfile(bundle, target)
        data = target.read_bytes()
        apps[app_id] = {"appId": app_id, "title": a["title"], "version": manifest["version"], "summary": "",
                        "requirements": manifest["requirements"], "target": "xtensa", "file": f"apps/{target.name}",
                        "sizeBytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
        info[app_id] = {"summary": a["summary"], "about": a["about"], "why": "Written from scratch for the Gotchi.",
                        "needs": a["needs"], "kind": a["kind"], "cover": f"covers/{app_id}.png",
                        "credit": {"own": True, "by": "Altio AI", "repo": REPO, "license": "Apache-2.0", "source": TREE + folder}}
        png = tmp / f"{folder}.png"
        subprocess.run(["magick", str(shots / f"{folder}.{a['shot']}.ppm"), str(png)], check=True)
        art = f'<image href="data:image/png;base64,{base64.b64encode(png.read_bytes()).decode()}" width="296" height="240"/>'
        covers.COVERS[app_id] = frame(a["bg"][0], a["bg"][1], gotchi(330, 30, 250, art), a["title"], a["tag"])
    index["apps"] = list(apps.values())
    index_path.write_text(json.dumps(index, indent=2, ensure_ascii=False))
    entries = (f'  {json.dumps(k)}: {{\n' + ",\n".join(f'    {json.dumps(x)}: {json.dumps(y, ensure_ascii=False)}' for x, y in v.items()) + "\n  }" for k, v in info.items())
    info_path.write_text("{\n" + ",\n".join(entries) + "\n}\n")
    pages = Path(tempfile.mkdtemp())
    for app_id, svg in covers.COVERS.items():
        (pages / f"{app_id}.html").write_text('<html><head><link href="https://fonts.googleapis.com/css2?family=Chakra+Petch:wght@700&family=IBM+Plex+Sans:wght@500&display=swap" rel="stylesheet"><style>html,body{margin:0;overflow:hidden}</style></head><body>' + svg + '</body></html>')
    subprocess.run(["uv", "run", "-q", "--with", "playwright", "python", "-I", str(Path(__file__).parent / "render_covers.py"), str(pages), str(SITE / "covers")], check=True)

if __name__ == "__main__":
    manifests() if sys.argv[1] == "manifests" else publish(sys.argv[2], sys.argv[3])
