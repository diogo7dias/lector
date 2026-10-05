"""Serve the simulated lector as a web page: the panel, its buttons, and what it is showing.

usage: python3 sim/server.py PORT

Boots the firmware from the last `pio run -e default` on a fresh card, then serves
  GET  /             the device page (panel image, one button per key)
  GET  /screen.png   the panel, upright
  GET  /state        {"frames", "activity", "ink"} as JSON
  POST /press/<key>  back, confirm, left, right, up, down, power
"""
import io, json, os, re, sys, tempfile, threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lector_sim import BUTTONS, LectorSim, make_flash, make_sd  # noqa: E402

KEYS = ["back", "confirm", "left", "right", "up", "down", "power"]
ACTIVITY = re.compile(r"Entering activity: (\w+)")

PAGE = """<!doctype html>
<html lang="en"><head><meta charset="utf-8"><title>lector sim</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
:root{--bg:#141311;--ink:#e6e1d5;--dim:#8a857a;--line:#3a3833;--paper:#f3f1ea}
body{margin:0;background:var(--bg);color:var(--ink);font:13px ui-monospace,monospace;display:flex;
 flex-direction:column;align-items:center;gap:12px;padding:16px}
h1{font-size:11px;letter-spacing:.2em;text-transform:uppercase;color:var(--dim);margin:0}
#screen{width:min(480px,100%);aspect-ratio:3/5;background:var(--paper);border:1px solid var(--line)}
.keys{display:grid;grid-template-columns:repeat(4,1fr);gap:6px;width:min(480px,100%)}
button{background:none;color:var(--ink);border:1px solid var(--line);padding:10px 0;font:inherit;cursor:pointer}
button:active{background:var(--line)}
dl{display:grid;grid-template-columns:auto 1fr;gap:2px 12px;margin:0;width:min(480px,100%)}
dt{color:var(--dim)}
</style></head><body>
<h1>lector &middot; xteink x4 &middot; qemu</h1>
<img id="screen" alt="Device screen" src="/screen.png">
<div class="keys" role="group" aria-label="Device buttons">
<button data-key="back">Back</button><button data-key="confirm">Confirm</button>
<button data-key="left">Left</button><button data-key="right">Right</button>
<button data-key="up">Up</button><button data-key="down">Down</button>
<button data-key="power">Power</button></div>
<dl><dt>activity</dt><dd data-testid="activity">-</dd>
<dt>frames</dt><dd data-testid="frames">0</dd>
<dt>ink</dt><dd data-testid="ink">0</dd>
<dt>busy</dt><dd data-testid="busy">no</dd></dl>
<script>
const $ = (s) => document.querySelector(s);
let frames = -1;
async function poll() {
  try {
    const s = await (await fetch('/state')).json();
    $('[data-testid=activity]').textContent = s.activity;
    $('[data-testid=ink]').textContent = s.ink;
    if (s.frames !== frames) { frames = s.frames; $('#screen').src = '/screen.png?f=' + frames; }
    $('[data-testid=frames]').textContent = s.frames;
  } catch (e) {}
  setTimeout(poll, 250);
}
async function press(key) {
  $('[data-testid=busy]').textContent = 'yes';
  await fetch('/press/' + key, { method: 'POST' });
  $('[data-testid=busy]').textContent = 'no';
}
document.querySelectorAll('[data-key]').forEach((b) => b.onclick = () => press(b.dataset.key));
const map = { ArrowUp: 'up', ArrowDown: 'down', ArrowLeft: 'left', ArrowRight: 'right',
  Enter: 'confirm', Escape: 'back', Backspace: 'back', p: 'power' };
addEventListener('keydown', (e) => { if (map[e.key]) { e.preventDefault(); press(map[e.key]); } });
poll();
</script></body></html>"""


class Device:
    def __init__(self, workdir):
        flash = make_flash(os.path.join(workdir, "flash.bin"))
        sd = make_sd(os.path.join(workdir, "sd.img"))
        self.sim = LectorSim(flash, sd, workdir)
        self.lock = threading.Lock()
        with self.lock:
            self.sim.boot()
            self.sim.wait_idle()

    def state(self):
        with self.lock:
            frames = self.sim.frames()
        seen = ACTIVITY.findall(self.sim.log_text())
        ink = 0
        if frames:
            ink = sum(1 for p in self.sim.screen().getdata() if p < 128)
        return {"frames": frames, "activity": seen[-1] if seen else "", "ink": ink}

    def png(self):
        buf = io.BytesIO()
        self.sim.screen().save(buf, "PNG")
        return buf.getvalue()

    def press(self, key):
        with self.lock:
            self.sim.press(key)


def handler(device):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def send(self, code, body, kind):
            self.send_response(code)
            self.send_header("Content-Type", kind)
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            path = self.path.split("?")[0]
            if path == "/":
                self.send(200, PAGE.encode(), "text/html; charset=utf-8")
            elif path == "/screen.png":
                self.send(200, device.png(), "image/png")
            elif path == "/state":
                self.send(200, json.dumps(device.state()).encode(), "application/json")
            else:
                self.send(404, b"not found", "text/plain")

        def do_POST(self):
            key = self.path.removeprefix("/press/")
            if key not in KEYS:
                self.send(400, b"unknown key", "text/plain")
                return
            device.press(key)
            self.send(200, json.dumps(device.state()).encode(), "application/json")

    return Handler


def main():
    port = int(sys.argv[1])
    workdir = os.environ.get("LECTOR_SIM_DIR") or tempfile.mkdtemp(prefix="lector-sim-")
    device = Device(workdir)
    server = ThreadingHTTPServer(("127.0.0.1", port), handler(device))
    print(f"lector sim on http://127.0.0.1:{port} ({workdir})", flush=True)
    try:
        server.serve_forever()
    finally:
        device.sim.stop()


if __name__ == "__main__":
    assert set(KEYS) == set(BUTTONS) | {"power"}
    main()
