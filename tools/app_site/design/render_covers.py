#!/usr/bin/env python3
"""Render cover HTML pages in one browser. render_covers.py <html-dir> <out-dir> (needs playwright and CHROME)."""
import os, sys
from pathlib import Path
from playwright.sync_api import sync_playwright
src, out = Path(sys.argv[1]), Path(sys.argv[2])
with sync_playwright() as p:
    b = p.chromium.launch(executable_path=os.environ["CHROME"])
    page = b.new_page(viewport={"width": 640, "height": 360})
    for f in sorted(src.glob("*.html")):
        page.goto(f.as_uri(), wait_until="load")
        page.evaluate("document.fonts.ready")
        page.screenshot(path=str(out / (f.stem + ".png")))
        print("wrote", f.stem)
    b.close()
