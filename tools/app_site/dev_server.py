#!/usr/bin/env python3
"""Local preview of the Gotchi App site, with the same /api relays as Vercel.

python3 tools/app_site/dev_server.py  then open http://localhost:8767 in Chrome.
"""
import re
import urllib.request
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlsplit

ROOT = Path(__file__).resolve().parent
UA = {"User-Agent": "Gotchi-App-Site/1.0"}


def upstream(url: str) -> bytes:
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=20) as r:
        return r.read()


class Handler(SimpleHTTPRequestHandler):
    def do_GET(self):
        parts = urlsplit(self.path)
        try:
            if parts.path == "/api/auth/me":
                # Local preview skips Google sign-in.
                return self.reply(b'{"email":"preview@localhost"}', "application/json")
            if parts.path == "/api/file":
                rel = parse_qs(parts.query).get("path", [""])[0]
                target = (ROOT / "private" / rel).resolve()
                if not str(target).startswith(str((ROOT / "private").resolve())) or not target.is_file():
                    return self.reply(b'{"error":"not found"}', "application/json", 404)
                kind = "application/json" if target.suffix == ".json" else "application/octet-stream"
                return self.reply(target.read_bytes(), kind)
            if parts.path == "/api/store":
                return self.reply(upstream("https://micropixel.ai/api/v1/store/apps"), "application/json")
            if parts.path == "/api/download":
                rid = parse_qs(parts.query).get("id", [""])[0]
                if not re.fullmatch(r"[0-9a-f-]{36}", rid):
                    return self.reply(b'{"error":"bad release id"}', "application/json", 400)
                return self.reply(upstream(f"https://micropixel.ai/api/v1/store/releases/{rid}/download"), "application/octet-stream")
        except Exception as error:
            return self.reply(str(error).encode(), "text/plain", 502)
        super().do_GET()

    def reply(self, body: bytes, kind: str, status: int = 200):
        self.send_response(status)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


if __name__ == "__main__":
    print("Gotchi App site on http://localhost:8767")
    ThreadingHTTPServer(("127.0.0.1", 8767), partial(Handler, directory=str(ROOT))).serve_forever()
