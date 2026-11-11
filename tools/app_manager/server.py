#!/usr/bin/env python3
"""MicroPixel App Manager: a local webpage to install and uninstall Gotchi Apps.

Run:  python3 tools/app_manager/server.py  then open http://localhost:8765

It lists every App folder in guest/apps, shows what is installed on the
connected Gotchi, and installs/uninstalls through tools/micropixel.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import threading
import time
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
APPS_DIR = REPO / "guest" / "apps"
CLI = REPO / "tools" / "micropixel"

def find_python() -> str:
    """The CLI needs pyserial for USB; prefer an interpreter that has it."""
    import os
    candidates = [os.environ.get("MICROPIXEL_PYTHON"), sys.executable,
                  *sorted(map(str, Path.home().glob(".espressif/python_env/idf*/bin/python")), reverse=True),
                  "/usr/bin/python3"]
    for candidate in filter(None, candidates):
        try:
            if subprocess.run([candidate, "-c", "import serial"], capture_output=True, timeout=10).returncode == 0:
                return candidate
        except (OSError, subprocess.TimeoutExpired):
            continue
    return sys.executable


def toolchain_env() -> dict:
    """Fill in WASI SDK and wamrc paths from the usual places when not already set."""
    import os
    env = dict(os.environ)
    if not env.get("WASI_SDK_PATH"):
        sdks = sorted(Path.home().glob("esp/toolchains/wasi-sdk-*"), reverse=True)
        if sdks:
            env["WASI_SDK_PATH"] = str(sdks[0])
    wamrc = REPO / "build" / "tools" / "wamrc-xtensa" / "wamrc-2.4.3"
    if wamrc.is_file():
        env.setdefault("XTENSA_WAMRC", str(wamrc))
        env.setdefault("WAMRC", str(wamrc))
    return env


PYTHON = find_python()
ENV = toolchain_env()
JOBS: dict[str, dict] = {}
JOBS_LOCK = threading.Lock()
DEVICE_LOCK = threading.Lock()  # one device command at a time
SETTINGS = {"port": None, "transport": None}


def title_of(manifest: dict, folder: str) -> str:
    title = manifest.get("title")
    if isinstance(title, str):
        return title
    if isinstance(title, dict):
        values = title.get("values") or {}
        return values.get(title.get("default", "en")) or next(iter(values.values()), folder)
    return folder


def local_apps() -> list[dict]:
    apps = []
    for app_json in sorted(APPS_DIR.glob("*/app.json")):
        try:
            manifest = json.loads(app_json.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            continue
        folder = app_json.parent.name
        apps.append({
            "folder": folder,
            "appId": manifest.get("app_id", folder),
            "title": title_of(manifest, folder),
            "version": manifest.get("version", ""),
            "description": str(manifest.get("description", ""))[:200],
        })
    return apps


def cli_args(*rest: str) -> list[str]:
    args = [PYTHON, str(CLI)]
    if SETTINGS["port"]:
        args += ["--port", SETTINGS["port"]]
    elif SETTINGS["transport"]:
        args += ["--transport", SETTINGS["transport"]]
    return args + list(rest)


def run_cli(*rest: str, timeout: float = 60) -> tuple[int, str, str]:
    with DEVICE_LOCK:
        proc = subprocess.run(cli_args(*rest), cwd=REPO, env=ENV, capture_output=True, text=True, timeout=timeout)
    return proc.returncode, proc.stdout, proc.stderr


def device_apps() -> dict:
    code, out, err = run_cli("app", "list")
    if code != 0:
        return {"ok": False, "error": (err or out).strip()[-800:] or "Device not reachable"}
    try:
        return {"ok": True, **json.loads(out)}
    except ValueError:
        return {"ok": False, "error": out.strip()[-800:]}


def ports() -> list:
    code, out, _ = run_cli("port", "list", timeout=20)
    try:
        return json.loads(out) if code == 0 else []
    except ValueError:
        return []


def start_job(label: str, *rest: str) -> str:
    job_id = uuid.uuid4().hex[:8]
    job = {"id": job_id, "label": label, "state": "running", "log": "", "started": time.time()}
    with JOBS_LOCK:
        JOBS[job_id] = job

    def work():
        try:
            with DEVICE_LOCK:
                proc = subprocess.Popen(cli_args(*rest), cwd=REPO, env=ENV, stdout=subprocess.PIPE,
                                        stderr=subprocess.STDOUT, text=True, bufsize=1)
                for line in proc.stdout:
                    job["log"] = (job["log"] + line)[-20000:]
                code = proc.wait()
            job["state"] = "done" if code == 0 else "failed"
        except Exception as error:  # report anything to the page
            job["log"] += f"\n{error}"
            job["state"] = "failed"

    threading.Thread(target=work, daemon=True).start()
    return job_id


def find_app(folder: str) -> Path | None:
    path = (APPS_DIR / folder).resolve()
    if path.parent != APPS_DIR.resolve() or not (path / "app.json").is_file():
        return None
    return path


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass

    def send(self, status: int, body: bytes, kind: str = "application/json") -> None:
        self.send_response(status)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def json(self, value, status: int = 200) -> None:
        self.send(status, json.dumps(value).encode())

    def body(self) -> dict:
        length = int(self.headers.get("Content-Length") or 0)
        try:
            return json.loads(self.rfile.read(length) or b"{}")
        except ValueError:
            return {}

    def do_GET(self):
        if self.path in ("/", "/index.html"):
            self.send(200, (HERE / "index.html").read_bytes(), "text/html; charset=utf-8")
        elif self.path == "/api/apps":
            self.json({"apps": local_apps()})
        elif self.path == "/api/device":
            self.json(device_apps())
        elif self.path == "/api/ports":
            self.json({"ports": ports(), "settings": SETTINGS})
        elif self.path.startswith("/api/jobs/"):
            job = JOBS.get(self.path.rsplit("/", 1)[-1])
            self.json(job or {"error": "unknown job"}, 200 if job else 404)
        else:
            self.json({"error": "not found"}, 404)

    def do_POST(self):
        data = self.body()
        if self.path == "/api/settings":
            SETTINGS["port"] = data.get("port") or None
            SETTINGS["transport"] = data.get("transport") or None
            self.json(SETTINGS)
        elif self.path == "/api/install":
            path = find_app(str(data.get("folder", "")))
            if not path:
                return self.json({"error": "unknown app"}, 400)
            args = ["app", "install", str(path)]
            if data.get("start"):
                args.append("--start")
            self.json({"job": start_job(f"Install {path.name}", *args)})
        elif self.path in ("/api/uninstall", "/api/start", "/api/stop"):
            app_id = str(data.get("appId", ""))
            if not app_id:
                return self.json({"error": "appId required"}, 400)
            action = self.path.rsplit("/", 1)[-1]
            self.json({"job": start_job(f"{action.title()} {app_id}", "app", action, app_id)})
        else:
            self.json({"error": "not found"}, 404)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--listen", type=int, default=8765)
    parser.add_argument("--port", help="USB serial port of the Gotchi (auto-detected when omitted)")
    parser.add_argument("--transport", choices=["usb", "remote"])
    args = parser.parse_args()
    SETTINGS["port"], SETTINGS["transport"] = args.port, args.transport or ("usb" if not args.port else None)
    server = ThreadingHTTPServer((args.host, args.listen), Handler)
    print(f"MicroPixel App Manager on http://{args.host}:{args.listen}")
    server.serve_forever()


if __name__ == "__main__":
    main()
