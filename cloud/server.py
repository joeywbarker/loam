#!/usr/bin/env python3
"""LOAM cloud server: keeps the world running 24/7, saves it, and serves a small API.

  LOAM_KEY   shared secret required on every /api call (header X-Loam-Key). Required.
  LOAM_DATA  directory for snapshots (default ./data); mount a persistent volume here.
  PORT       listen port (default 8080).
"""
import ctypes, hmac, json, os, signal, struct, sys, threading, time, datetime, glob
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

HERE = os.path.dirname(os.path.abspath(__file__))
TICK_HZ = 30
SAVE_EVERY = 10.0
KEEP_BACKUPS = 7
MAX_VIEW_CELLS = 24000
MAX_BODY = 4096


class World:
    def __init__(self, data_dir, lib_path=None):
        self.lib = ctypes.CDLL(lib_path or os.path.join(HERE, "libloam.so"))
        L = self.lib
        L.lw_init.argtypes = [ctypes.c_uint32]
        L.lw_tick.argtypes = [ctypes.c_int]
        L.lw_tickcount.restype = ctypes.c_uint32
        L.lw_stats.argtypes = [ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int)]
        L.lw_view.argtypes = [ctypes.c_int] * 4 + [ctypes.c_void_p]
        L.lw_type.argtypes = [ctypes.c_int] * 4
        L.lw_erase.argtypes = [ctypes.c_int] * 2
        L.lw_breathe.argtypes = [ctypes.c_int] * 4
        L.lw_save.argtypes = [ctypes.c_char_p]
        L.lw_load.argtypes = [ctypes.c_char_p]
        self.W, self.H = L.lw_width(), L.lw_height()
        self.lock = threading.Lock()
        self.data_dir = data_dir
        os.makedirs(os.path.join(data_dir, "backups"), exist_ok=True)
        self.path = os.path.join(data_dir, "world.bin")
        L.lw_init(int.from_bytes(os.urandom(4), "little") | 1)
        self.loaded = L.lw_load(self.path.encode()) == 0
        self.last_save = time.monotonic()
        self.last_backup_day = None
        self.stop = False

    # -- simulation thread --
    def run(self):
        period = 1.0 / TICK_HZ
        nxt = time.monotonic()
        while not self.stop:
            now = time.monotonic()
            behind = int((now - nxt) / period)
            n = max(1, min(behind + 1, 5))          # catch up a little, never spiral
            if now >= nxt:
                with self.lock:
                    self.lib.lw_tick(n)
                nxt += n * period
                if now - nxt > 1.0: nxt = now        # we were suspended: don't replay history
            time.sleep(max(0.0, min(period / 2, nxt - time.monotonic())))
            if time.monotonic() - self.last_save >= SAVE_EVERY:
                self.save()

    def save(self):
        with self.lock:
            rc = self.lib.lw_save(self.path.encode())
        self.last_save = time.monotonic()
        if rc != 0:
            print("save failed", rc, file=sys.stderr); return False
        day = datetime.date.today().isoformat()
        if day != self.last_backup_day:
            bp = os.path.join(self.data_dir, "backups", f"world-{day}.bin")
            if not os.path.exists(bp):
                try:
                    tmp = bp + ".tmp"
                    with open(self.path, "rb") as a, open(tmp, "wb") as b: b.write(a.read())
                    os.replace(tmp, bp)
                except OSError as e:
                    print("backup failed", e, file=sys.stderr)
            self.last_backup_day = day
            for old in sorted(glob.glob(os.path.join(self.data_dir, "backups", "world-*.bin")))[:-KEEP_BACKUPS]:
                try: os.remove(old)
                except OSError: pass
        return True

    # -- api --
    def stats(self):
        w, e = ctypes.c_int(), ctypes.c_int()
        with self.lock:
            self.lib.lw_stats(ctypes.byref(w), ctypes.byref(e))
            t = self.lib.lw_tickcount()
        return t, w.value, e.value

    def view(self, x, y, w, h):
        buf = (ctypes.c_uint16 * (w * h))()
        with self.lock:
            if self.lib.lw_view(x, y, w, h, buf) != 0: return None
            t = self.lib.lw_tickcount()
            wc, ec = ctypes.c_int(), ctypes.c_int()
            self.lib.lw_stats(ctypes.byref(wc), ctypes.byref(ec))
        head = struct.pack("<IIIHHHH", t, wc.value, ec.value, w, h, self.W, self.H)
        return head + bytes(buf)

    def act(self, d):
        op = d.get("op")
        x, y, ink = int(d.get("x", 0)), int(d.get("y", 0)), int(d.get("ink", 15)) & 15
        direction = int(d.get("dir", 0)) & 3
        with self.lock:
            if op == "text":
                s = str(d.get("s", ""))[:200]
                dx, dy = ((1, 0), (0, 1), (-1, 0), (0, -1))[direction]
                n = 0
                for i, c in enumerate(s):
                    if self.lib.lw_type(x + dx * i, y + dy * i, ord(c), ink) == 0: n += 1
                return {"ok": True, "typed": n}
            if op == "erase":
                self.lib.lw_erase(x, y); return {"ok": True}
            if op == "breathe":
                return {"ok": self.lib.lw_breathe(x, y, direction, ink) == 0}
        return {"ok": False, "error": "unknown op"}


def make_handler(world, key, static_path):
    class H(BaseHTTPRequestHandler):
        server_version = "loam"
        def log_message(self, *a): pass

        def _cors(self):
            self.send_header("Access-Control-Allow-Origin", "*")
            self.send_header("Access-Control-Allow-Headers", "X-Loam-Key, Content-Type")
            self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
            self.send_header("Cache-Control", "no-store")

        def _send(self, code, body, ctype="application/json"):
            if isinstance(body, (dict, list)): body = json.dumps(body).encode()
            self.send_response(code); self._cors()
            self.send_header("Content-Type", ctype); self.send_header("Content-Length", str(len(body)))
            self.end_headers(); self.wfile.write(body)

        def _authed(self):
            got = self.headers.get("X-Loam-Key", "")
            return hmac.compare_digest(got.encode(), key.encode())

        def do_OPTIONS(self):
            self.send_response(204); self._cors(); self.send_header("Content-Length", "0"); self.end_headers()

        def do_GET(self):
            u = urlparse(self.path)
            if u.path in ("/", "/index.html"):
                try:
                    with open(static_path, "rb") as f: return self._send(200, f.read(), "text/html; charset=utf-8")
                except OSError: return self._send(404, {"error": "no app bundled"})
            if u.path == "/healthz": return self._send(200, b"ok", "text/plain")
            if not self._authed(): return self._send(401, {"error": "bad key"})
            if u.path == "/api/view":
                q = parse_qs(u.query)
                try:
                    x, y, w, h = (int(q[k][0]) for k in ("x", "y", "w", "h"))
                except (KeyError, ValueError): return self._send(400, {"error": "x,y,w,h required"})
                if w < 1 or h < 1 or w > world.W or h > world.H or w * h > MAX_VIEW_CELLS:
                    return self._send(400, {"error": "bad window"})
                body = world.view(x, y, w, h)
                return self._send(200, body, "application/octet-stream")
            if u.path == "/api/stats":
                t, w, e = world.stats(); return self._send(200, {"tick": t, "wisps": w, "energy": e, "w": world.W, "h": world.H})
            self._send(404, {"error": "not found"})

        def do_POST(self):
            if urlparse(self.path).path != "/api/act": return self._send(404, {"error": "not found"})
            if not self._authed(): return self._send(401, {"error": "bad key"})
            try:
                n = int(self.headers.get("Content-Length", "0"))
                if n < 0 or n > MAX_BODY: return self._send(413, {"error": "too large"})
                d = json.loads(self.rfile.read(n) or b"{}")
                if not isinstance(d, dict): raise ValueError
                self._send(200, world.act(d))
            except (ValueError, TypeError): self._send(400, {"error": "bad request"})
    return H


def main():
    key = os.environ.get("LOAM_KEY", "")
    if len(key) < 12 and os.environ.get("LOAM_ALLOW_WEAK") != "1":
        sys.exit("LOAM_KEY must be set to a secret of at least 12 characters (this server can modify your world).")
    data = os.environ.get("LOAM_DATA", os.path.join(HERE, "data"))
    port = int(os.environ.get("PORT", "8080"))
    cands = [os.path.join(HERE, "app", "index.html"), os.path.join(HERE, "..", "app", "web", "index.html")]
    static = os.environ.get("LOAM_APP") or next((c for c in cands if os.path.exists(c)), cands[0])
    world = World(data)
    print(f"loam: world {world.W}x{world.H}, {'restored from snapshot' if world.loaded else 'new world'}, listening on :{port}", flush=True)
    threading.Thread(target=world.run, daemon=True).start()
    srv = ThreadingHTTPServer(("0.0.0.0", port), make_handler(world, key, static))
    signal.signal(signal.SIGTERM, lambda *a: threading.Thread(target=srv.shutdown, daemon=True).start())
    try: srv.serve_forever()
    except KeyboardInterrupt: pass
    finally:
        world.stop = True; world.save(); print("loam: saved, bye", flush=True)

if __name__ == "__main__":
    main()
