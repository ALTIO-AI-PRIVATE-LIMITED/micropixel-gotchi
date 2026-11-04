#!/usr/bin/env python3
"""Package every guest/apps project for the ESP32-S3 Gotchi and publish it into
tools/app_site/apps/ with an index.json the site reads.

Needs WASI_SDK_PATH and XTENSA_WAMRC (the GitHub Action sets both).
"""
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
OUT = Path(__file__).resolve().parent / "private" / "apps"
GOTCHI_W, GOTCHI_H = 296, 240  # Cheeko Gotchi screen


def title(manifest, folder):
    t = manifest.get("title")
    if isinstance(t, dict):
        values = t.get("values") or {}
        return values.get(t.get("default", "en")) or next(iter(values.values()), folder)
    return t or folder


def main():
    OUT.mkdir(exist_ok=True)
    only = set(sys.argv[1:])
    index, failed = [], []
    for app_json in sorted((REPO / "guest/apps").glob("*/app.json")):
        folder = app_json.parent.name
        if only and folder not in only:
            continue
        manifest = json.loads(app_json.read_text())
        display = manifest.get("requirements", {}).get("display", {})
        mw, mh = display.get("min_width", 0), display.get("min_height", 0)
        if not ((GOTCHI_W >= mw and GOTCHI_H >= mh) or (GOTCHI_H >= mw and GOTCHI_W >= mh)):
            print(f"--  {folder}: needs {mw}x{mh}, skipped (Gotchi is {GOTCHI_W}x{GOTCHI_H})")
            continue
        bundle = OUT / f"{folder}.bundle.bin"
        print(f"==> {folder}", flush=True)
        result = subprocess.run([sys.executable, str(REPO / "tools/micropixel"), "package", str(app_json.parent),
                                 "--aot-target", "xtensa", "--output", str(bundle)], cwd=REPO)
        if result.returncode != 0 or not bundle.is_file():
            failed.append(folder)
            continue
        data = bundle.read_bytes()
        index.append({
            "appId": manifest.get("app_id", folder),
            "title": title(manifest, folder),
            "version": manifest.get("version", ""),
            "summary": manifest.get("description", ""),
            "requirements": manifest.get("requirements", {}),
            "target": "xtensa",
            "file": f"apps/{bundle.name}",
            "sizeBytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
        })
    (OUT / "index.json").write_text(json.dumps({"apps": index}, indent=2, ensure_ascii=False))
    print(f"Packaged {len(index)} apps" + (f"; failed: {', '.join(failed)}" if failed else ""))
    return 1 if failed and not index else 0


if __name__ == "__main__":
    sys.exit(main())
