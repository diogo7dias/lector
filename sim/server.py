"""Serve the simulated lector as a web page: the panel, its buttons, and what it is showing.

usage: LECTOR_BOARD=x4pro|x3|x4 python3 sim/server.py PORT   (default x4pro)

Boots that board's firmware from its last `pio run` on a fresh card, then serves
  GET  /             the device page (panel image, one button per key; click the panel to touch it)
  GET  /screen.png   the panel, upright
  GET  /state        {"frames", "activity", "ink"} as JSON
  GET  /ink/<x>/<y>/<w>/<h>  {"ink", "area"}: dark pixels in that box of the upright panel
  POST /press/<key>  the board's keys (see lector_sim.buttons)
  POST /tap/<x>/<y>  touch the upright panel (480x800), touch boards only
  POST /swipe/<x1>/<y1>/<x2>/<y2>  slide a finger across it
"""
import io, json, os, re, sys, tempfile, threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lector_sim import BOARDS, LectorSim, buttons, make_flash, make_sd  # noqa: E402

BOARD = os.environ.get("LECTOR_BOARD", "x4pro")
KEYS = buttons(BOARD)
ACTIVITY = re.compile(r"Entering activity: (\w+)")

PAGE = """<!doctype html>
<html lang="en"><head><meta charset="utf-8"><title>lector sim</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
:root{--bg:#141311;--ink:#e6e1d5;--dim:#8a857a;--line:#3a3833;--paper:#f3f1ea}
body{margin:0;background:var(--bg);color:var(--ink);font:13px ui-monospace,monospace;display:flex;
 flex-direction:column;align-items:center;gap:12px;padding:16px}
h1{font-size:11px;letter-spacing:.2em;text-transform:uppercase;color:var(--dim);margin:0}
#screen{width:min(480px,100%);background:var(--paper);border:1px solid var(--line);cursor:crosshair}
.keys{display:grid;grid-template-columns:repeat(4,1fr);gap:6px;width:min(480px,100%)}
button{background:none;color:var(--ink);border:1px solid var(--line);padding:10px 0;font:inherit;cursor:pointer}
button:active{background:var(--line)}
dl{display:grid;grid-template-columns:auto 1fr;gap:2px 12px;margin:0;width:min(480px,100%)}
dt{color:var(--dim)}
</style></head><body>
<h1>lector &middot; xteink BOARD &middot; qemu</h1>
<img id="screen" alt="Device screen" src="/screen.png">
<div class="keys" role="group" aria-label="Device buttons">
KEYS</div>
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
// The panel takes touch: a press and release in place is a tap, anything longer a swipe.
const at = (e) => {
  const r = $('#screen').getBoundingClientRect();
  return [Math.round((e.clientX - r.left) * 480 / r.width), Math.round((e.clientY - r.top) * 800 / r.height)];
};
let from = null;
$('#screen').ondragstart = (e) => e.preventDefault();
$('#screen').onpointerdown = (e) => { from = at(e); };
$('#screen').onpointerup = async (e) => {
  if (!from) return;
  const to = at(e), [x, y] = from;
  from = null;
  const path = Math.hypot(to[0] - x, to[1] - y) < 12 ? `/tap/${x}/${y}` : `/swipe/${x}/${y}/${to[0]}/${to[1]}`;
  $('[data-testid=busy]').textContent = 'yes';
  await fetch(path, { method: 'POST' });
  $('[data-testid=busy]').textContent = 'no';
};
const map = { ArrowUp: 'up', ArrowDown: 'down', ArrowLeft: 'left', ArrowRight: 'right',
  Enter: 'confirm', Escape: 'back', Backspace: 'back', h: 'home', p: 'power' };
addEventListener('keydown', (e) => { if (map[e.key]) { e.preventDefault(); press(map[e.key]); } });
poll();
</script></body></html>"""


class Device:
    def __init__(self, workdir):
        flash = make_flash(os.path.join(workdir, "flash.bin"), BOARD)
        sd = make_sd(os.path.join(workdir, "sd.img"))
        self.sim = LectorSim(BOARD, flash, sd, workdir)
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

    def ink(self, box):
        region = self.sim.screen().crop(box)
        return {"ink": sum(1 for p in region.getdata() if p < 128), "area": region.width * region.height}

    def png(self):
        buf = io.BytesIO()
        self.sim.screen().save(buf, "PNG")
        return buf.getvalue()

    def press(self, key):
        with self.lock:
            self.sim.press(key)

    def swipe(self, *points):
        with self.lock:
            self.sim.swipe(*points)


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
                keys = "".join(f'<button data-key="{k}">{k.capitalize()}</button>' for k in KEYS)
                page = PAGE.replace("BOARD", BOARD.replace("x4pro", "x4 pro")).replace("KEYS", keys)
                self.send(200, page.encode(), "text/html; charset=utf-8")
            elif path == "/screen.png":
                self.send(200, device.png(), "image/png")
            elif path == "/state":
                self.send(200, json.dumps(device.state()).encode(), "application/json")
            elif path.startswith("/ink/"):
                try:
                    x, y, w, h = map(int, path.split("/")[2:])
                except ValueError:
                    self.send(400, b"want /ink/x/y/w/h", "text/plain")
                    return
                self.send(200, json.dumps(device.ink((x, y, x + w, y + h))).encode(), "application/json")
            else:
                self.send(404, b"not found", "text/plain")

        def do_POST(self):
            parts = self.path.strip("/").split("/")
            if (parts[0], len(parts)) in (("tap", 3), ("swipe", 5)) and BOARDS[BOARD].get("touch"):
                try:
                    points = [int(v) for v in parts[1:]]
                except ValueError:
                    points = [-1]
                if not all(0 <= v < 480 for v in points[0::2]) or not all(0 <= v < 800 for v in points[1::2]):
                    self.send(400, b"bad point", "text/plain")
                    return
                device.swipe(*(points * 2 if len(points) == 2 else points))
            elif parts[0] == "press" and len(parts) == 2 and parts[1] in KEYS:
                device.press(parts[1])
            else:
                self.send(400, b"unknown key", "text/plain")
                return
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
    main()
