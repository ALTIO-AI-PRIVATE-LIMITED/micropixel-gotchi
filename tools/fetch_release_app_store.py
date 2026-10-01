#!/usr/bin/env python3
"""Extract the prebuilt ESP32-S3 App Store from an official MicroPixel release.

Every 16 MiB S3 board shares one app_store geometry and Xtensa AOT Bundles, so
the store inside the SZPI full image installs unchanged on other S3 boards such
as the Cheeko Gotchi. This avoids building the Xtensa wamrc (an LLVM build) just
to get the stock Apps. The image is padded to the whole partition with erased
bytes, so flashing it also clears whatever an earlier firmware left there.
"""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import re
import urllib.request
import zipfile
from pathlib import Path


REPOSITORY = "78/micropixel"
SOURCE_PROFILE = "szpi-esp32s3"
APP_STORE_OFFSET = 0x800000
APP_STORE_SIZE = 0x800000
CATALOG_MAGIC = b"MPBUNDLE"
WORKSPACE_ROOT = Path(__file__).resolve().parents[1]


def firmware_version() -> str:
    cmake = (WORKSPACE_ROOT / "firmware/espressif/CMakeLists.txt").read_text()
    match = re.search(r'set\(PROJECT_VER "([^"]+)"\)', cmake)
    if match is None:
        raise SystemExit("cannot read PROJECT_VER from firmware/espressif/CMakeLists.txt")
    return match.group(1)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--version", default=firmware_version(), help="firmware release (default: this tree's)")
    parser.add_argument("--output", type=Path, default=WORKSPACE_ROOT / "build/esp32s3-apps/app-store.bin")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    asset = f"micropixel-firmware-{args.version}-{SOURCE_PROFILE}.zip"
    url = f"https://github.com/{REPOSITORY}/releases/download/firmware-v{args.version}/{asset}"
    print(f"==> Downloading {url}")
    with urllib.request.urlopen(url) as response:
        archive = zipfile.ZipFile(io.BytesIO(response.read()))

    manifest = json.loads(archive.read("manifest.json"))
    full_image = archive.read("micropixel-full.bin")
    expected = manifest["files"]["micropixel-full.bin"]["sha256"]
    if hashlib.sha256(full_image).hexdigest() != expected:
        raise SystemExit("micropixel-full.bin does not match the release manifest")
    if manifest.get("target") != "esp32s3":
        raise SystemExit(f"release target is {manifest.get('target')}, expected esp32s3")

    store = full_image[APP_STORE_OFFSET:]
    if not store.startswith(CATALOG_MAGIC) or len(store) > APP_STORE_SIZE:
        raise SystemExit("release image has no S3 app_store at 0x800000")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(store + b"\xff" * (APP_STORE_SIZE - len(store)))
    print(f"==> Wrote {args.output} ({len(store)} bytes of Apps from firmware {manifest['firmware_version']})")


if __name__ == "__main__":
    main()
