"""Takes the Store screenshots (task 8-5d) from the real app.

The app is started twice on a library of its own (--library-dir, so the
user's library and settings are not touched): once in Pure mode (the
standard view, the library view, the signal path) and once in Processing
mode with the SE 300B tube (the amp view, ABX). Its WebView2 is opened for
remote debugging on 127.0.0.1 only, and this script drives the page from
there: it picks the language and the view, plays a track, opens the
dialogs and captures the page at 1920 x 1080 (1600 x 900 CSS pixels at
1.2). The page's own storage (the language, the view) is shared with the
app as the user runs it, so it is read first and put back at the end.

Steps (packaging/store/README.md, "画面写真"):

    pip install numpy soundfile mutagen pillow websockets
    python tools/make_store_demo.py DEMO\\Music
    subst M: DEMO                      # the watched folder then reads M:\\Music, not a user's path
    ap_lib scan M:\\Music --db M:\\data\\library.db
    ap_lib analyze --db M:\\data\\library.db
    python tools/store_screenshots.py --device-id ID --device-name NAME
    subst M: /d

The track plays on the device given (ap_play --list shows the ids), so
sound comes out of it while the screenshots are taken.
"""

from __future__ import annotations

import argparse
import asyncio
import base64
import json
import os
import subprocess
import time
import urllib.request
from pathlib import Path

import websockets

ROOT = Path(__file__).resolve().parent.parent
PORT = 9333
W, H, SCALE = 1600, 900, 1.2
ALBUM = "Lanterns Over Kesseldorf"
TRACK = "Slow Tram Home"
TRACK_INDEX = 2  # its row in the album
LANGS = {"ja": "ja-JP", "en": "en-US"}


class Page:
    def __init__(self, ws):
        self.ws = ws
        self.n = 0

    async def call(self, method, **params):
        self.n += 1
        me = self.n
        await self.ws.send(json.dumps({"id": me, "method": method, "params": params}))
        while True:
            m = json.loads(await self.ws.recv())
            if m.get("id") == me:
                if "error" in m:
                    raise RuntimeError(m["error"])
                return m["result"]

    async def js(self, expr):
        r = await self.call("Runtime.evaluate", expression=expr, awaitPromise=True, returnByValue=True)
        if "exceptionDetails" in r:
            raise RuntimeError(r["exceptionDetails"])
        return r["result"].get("value")

    async def until(self, cond, timeout=20.0):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if await self.js(cond):
                return
            await asyncio.sleep(0.25)
        state = await self.js(
            "[document.querySelector('footer.statusbar')?.innerText, document.getElementById('file-name')?.textContent,"
            " document.getElementById('output')?.innerText].join(' | ')")
        raise TimeoutError(f"{cond}\n  the page shows: {state}")

    async def size(self):
        await self.call("Emulation.setDeviceMetricsOverride", width=W, height=H, deviceScaleFactor=SCALE, mobile=False)

    async def capture(self, path: Path):
        await self.js("window.dispatchEvent(new Event('resize')); true")
        await asyncio.sleep(3.0)  # the amp's pictures are sized after a resize; the meters move a little
        r = await self.call("Page.captureScreenshot", format="png", fromSurface=True)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(base64.b64decode(r["data"]))
        print("  ", path.relative_to(ROOT) if path.is_relative_to(ROOT) else path)


def find(selector, text):
    return f"[...document.querySelectorAll({json.dumps(selector)})].find(e => e.textContent.includes({json.dumps(text)}))"


async def show(p: Page, lang: str, view: str):
    await p.js(f"localStorage.setItem('language', {json.dumps(lang)}); localStorage.setItem('uiView', {json.dumps(view)});"
               " location.reload(); true")
    await asyncio.sleep(2.5)
    await p.until("document.querySelectorAll('#table-body > *, #grid .tile').length > 0")
    await p.size()
    await asyncio.sleep(1.0)


async def open_album_and_play(p: Page):
    playing = await p.js("document.getElementById('file-name')?.textContent || ''")
    await p.js("document.querySelector('.nav[data-view=\"albums\"]').click(); true")
    await p.until(f"!!({find('#grid .tile', ALBUM)})")
    await p.js(f"({find('#grid .tile', ALBUM)}).click(); true")
    # The album's rows come a moment after the click: wait for the track at its place in the album.
    row = f'#table-body .row[data-index="{TRACK_INDEX}"]'
    await p.until(f"!!({find(row, TRACK)})")
    if TRACK in playing:
        return
    await p.js(f"for (const [type, detail] of [['click', 1], ['click', 2], ['dblclick', 2]]) "
               f"({find(row, TRACK)}).dispatchEvent(new MouseEvent(type, {{bubbles: true, detail}})); true")
    await p.until(f"(document.getElementById('file-name')?.textContent || '').includes({json.dumps(TRACK)})"
                  " && !document.getElementById('seek').disabled")
    await asyncio.sleep(0.5)
    await p.js("const s = document.getElementById('seek'); s.value = '280';"
               " s.dispatchEvent(new Event('input')); s.dispatchEvent(new Event('change')); true")
    await asyncio.sleep(1.0)


async def pure(p: Page, out: Path):
    for lang, folder in LANGS.items():
        await show(p, lang, "standard")
        await open_album_and_play(p)
        await p.capture(out / folder / "01-standard.png")
        await show(p, lang, "library")
        await p.js("document.querySelector('.nav[data-view=\"tracks\"]').click(); true")
        await p.capture(out / folder / "03-library.png")
        await show(p, lang, "standard")
        await p.js("document.getElementById('output').click(); true")
        await p.until("document.getElementById('output-dialog').open")
        await p.capture(out / folder / "04-signal-path.png")
        await p.js("document.getElementById('output-dialog').close(); true")


async def processed(p: Page, out: Path):
    for lang, folder in LANGS.items():
        await show(p, lang, "amp")
        await open_album_and_play(p)
        await p.capture(out / folder / "02-amp.png")
        await p.js("document.getElementById('settings-abx').click(); true")
        await p.until("document.getElementById('abx-dialog').open")
        await p.capture(out / folder / "05-abx.png")
        await p.js("document.getElementById('abx-dialog').close(); true")


def settings(device_id: str, device_name: str, processing: bool) -> str:
    lines = ["# Settings (task 6-3). key=value, one per line.", "output.kind=wasapi", f"output.id={device_id}",
             f"output.name={device_name}", "output.dop=0"]
    if processing:
        lines += ["mode=processed", "tube.enabled=1", "tube.type=se300b", "tube.drive=-6"]
    return "\n".join(lines) + "\n"


def page_socket(timeout=30.0) -> str:
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        try:
            pages = json.load(urllib.request.urlopen(f"http://127.0.0.1:{PORT}/json", timeout=2))
            for pg in pages:
                if pg["type"] == "page" and pg["url"].startswith("https://juce.backend"):
                    return pg["webSocketDebuggerUrl"]
        except OSError:
            pass
        time.sleep(0.5)
    raise TimeoutError("the app's page did not come up")


async def session(exe: Path, data: Path, text: str, work, saved: dict | None) -> dict:
    (data / "settings.txt").write_text(text, encoding="utf-8")
    if port_open():
        raise SystemExit(f"port {PORT} is in use: close the app (or whatever holds it) first")
    env = dict(os.environ, WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS=f"--remote-debugging-port={PORT}")
    app = subprocess.Popen([str(exe), f"--library-dir={data}"], env=env)
    try:
        async with websockets.connect(page_socket(), max_size=64 * 1024 * 1024) as ws:
            p = Page(ws)
            # The page is listed before it has started: wait for the library, then a little more.
            await p.until("document.querySelectorAll('#table-body > *, #grid .tile').length > 0", timeout=60.0)
            await asyncio.sleep(3.0)
            before = await p.js("JSON.stringify(Object.fromEntries(Object.keys(localStorage).map(k => [k, localStorage.getItem(k)])))")
            first = json.loads(before) if saved is None else saved
            try:
                await work(p)
            finally:
                await p.js(f"localStorage.clear(); for (const [k, v] of Object.entries({json.dumps(first)})) localStorage.setItem(k, v); true")
            return first
    finally:
        close(app)


def port_open() -> bool:
    try:
        urllib.request.urlopen(f"http://127.0.0.1:{PORT}/json", timeout=1)
        return True
    except OSError:
        return False


def close(app: subprocess.Popen):
    """Closes the window as a user would, then waits until its WebView2 has gone too (the next start shares its folder)."""
    subprocess.run(["taskkill", "/PID", str(app.pid)], capture_output=True)
    try:
        app.wait(timeout=20)
    except subprocess.TimeoutExpired:
        app.kill()
        app.wait()
    end = time.monotonic() + 30
    while port_open() and time.monotonic() < end:
        time.sleep(0.5)
    time.sleep(2.0)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--data", default=r"M:\data", help="the demo library's folder (--library-dir)")
    ap.add_argument("--exe", default=str(ROOT / "build/vs2022/app/ap_app_artefacts/Release/DEZIGNAudioPlayer.exe"))
    ap.add_argument("--out", default=str(ROOT / "packaging/store/screenshots"))
    ap.add_argument("--device-id", required=True, help="a WASAPI output's id (ap_play --list)")
    ap.add_argument("--device-name", required=True)
    ap.add_argument("--phase", choices=["both", "pure", "processed"], default="both",
                    help="pure: the standard view, the library view, the signal path; processed: the amp view, ABX")
    a = ap.parse_args()
    data, out, exe = Path(a.data), Path(a.out), Path(a.exe)
    if not (data / "library.db").exists():
        raise SystemExit(f"{data / 'library.db'} is missing: run make_store_demo.py, subst and ap_lib first")
    saved = None
    if a.phase in ("both", "pure"):
        saved = asyncio.run(session(exe, data, settings(a.device_id, a.device_name, False), lambda p: pure(p, out), saved))
    if a.phase in ("both", "processed"):
        asyncio.run(session(exe, data, settings(a.device_id, a.device_name, True), lambda p: processed(p, out), saved))


if __name__ == "__main__":
    main()
