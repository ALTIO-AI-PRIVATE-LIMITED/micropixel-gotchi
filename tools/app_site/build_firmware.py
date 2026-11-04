#!/usr/bin/env python3
"""Publish the built Cheeko Gotchi firmware into tools/app_site/private/firmware.

Run after `tools/firmware.py cheeko-gotchi build`. Writes the update parts
(bootloader, partition table, OTA data, app) and manifest.json for the site.
"Update" leaves the App Store alone; "Full reset" erases the chip first and
flashes the same parts, so the Gotchi starts with no apps.
"""
import datetime
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
BUILD = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else REPO / "build/host-esp32s3-gotchi"
OUT = Path(__file__).resolve().parent / "private/firmware"


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    args = json.loads((BUILD / "flasher_args.json").read_text())
    update = []
    for offset, rel in sorted(args["flash_files"].items(), key=lambda item: int(item[0], 16)):
        name = Path(rel).name
        shutil.copyfile(BUILD / rel, OUT / name)
        update.append({"file": name, "offset": int(offset, 16)})
    cmake = (REPO / "firmware/espressif/CMakeLists.txt").read_text()
    version = re.search(r'set\(PROJECT_VER "([^"]+)"\)', cmake).group(1)
    commit = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=REPO, capture_output=True, text=True).stdout.strip()
    manifest = {"version": version, "commit": commit, "built": datetime.date.today().isoformat(),
                "update": update, "full": update}
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=2))
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
