import json, os, signal, socket, struct, subprocess, sys, tempfile, time, unittest, urllib.request, urllib.error
HERE = os.path.dirname(os.path.abspath(__file__))
KEY = "test-key-0123456789"

def free_port():
    s = socket.socket(); s.bind(("127.0.0.1", 0)); p = s.getsockname()[1]; s.close(); return p

class Srv:
    def __init__(self, data):
        self.port = free_port()
        env = dict(os.environ, LOAM_KEY=KEY, LOAM_DATA=data, PORT=str(self.port))
        self.p = subprocess.Popen([sys.executable, os.path.join(HERE, "server.py")], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        for _ in range(100):
            try: urllib.request.urlopen(f"http://127.0.0.1:{self.port}/healthz", timeout=0.5); return
            except Exception: time.sleep(0.1)
        raise RuntimeError("server did not start")
    def req(self, path, data=None, key=KEY):
        r = urllib.request.Request(f"http://127.0.0.1:{self.port}{path}", data=None if data is None else json.dumps(data).encode(),
                                   headers={"X-Loam-Key": key} if key is not None else {})
        with urllib.request.urlopen(r, timeout=5) as resp: return resp.status, resp.read()
    def view(self, x, y, w, h):
        _, b = self.req(f"/api/view?x={x}&y={y}&w={w}&h={h}")
        t, wisps, energy, vw, vh, W, H = struct.unpack("<IIIHHHH", b[:20])
        cells = struct.unpack(f"<{vw*vh}H", b[20:]); return dict(tick=t, wisps=wisps, energy=energy, w=vw, h=vh, W=W, H=H, cells=cells)
    def stop(self):
        self.p.send_signal(signal.SIGTERM)
        try: return self.p.wait(timeout=10)
        except subprocess.TimeoutExpired: self.p.kill(); return -9

class T(unittest.TestCase):
    def test_refuses_to_start_without_key(self):
        env = {k: v for k, v in os.environ.items() if k != "LOAM_KEY"}
        r = subprocess.run([sys.executable, os.path.join(HERE, "server.py")], env=env, capture_output=True, timeout=10)
        self.assertNotEqual(r.returncode, 0)

    def test_auth_view_act_persist(self):
        with tempfile.TemporaryDirectory() as d:
            s = Srv(d)
            try:
                with self.assertRaises(urllib.error.HTTPError) as c: s.req("/api/view?x=0&y=0&w=4&h=4", key="wrong")
                self.assertEqual(c.exception.code, 401)
                with self.assertRaises(urllib.error.HTTPError) as c: s.req("/api/view?x=0&y=0&w=4&h=4", key=None)
                self.assertEqual(c.exception.code, 401)
                with self.assertRaises(urllib.error.HTTPError) as c: s.req("/api/view?x=0&y=0&w=9999&h=4")
                self.assertEqual(c.exception.code, 400)
                v = s.view(0, 0, 40, 20)
                self.assertEqual((v["w"], v["h"], v["W"], v["H"], len(v["cells"])), (40, 20, 256, 256, 800))
                self.assertGreater(v["wisps"], 10)
                # type text far from the colonies, then read it back (also checks wrap-around: x=-3 -> 253)
                _, b = s.req("/api/act", {"op": "text", "x": -3, "y": 200, "dir": 0, "ink": 15, "s": "hello"})
                self.assertEqual(json.loads(b)["typed"], 5)
                v = s.view(253, 200, 10, 1)
                self.assertEqual("".join(chr(c & 0xFF) for c in v["cells"][:5]), "hello")
                _, b = s.req("/api/act", {"op": "breathe", "x": 100, "y": 220, "dir": 0, "ink": 12})
                self.assertTrue(json.loads(b)["ok"])
                t0 = s.view(0, 0, 1, 1)["tick"]; time.sleep(1.0); t1 = s.view(0, 0, 1, 1)["tick"]
                self.assertGreaterEqual(t1 - t0, 20)       # ~30 Hz
            finally:
                code = s.stop()
            self.assertEqual(code, 0)                       # clean SIGTERM exit (saved)
            self.assertTrue(os.path.exists(os.path.join(d, "world.bin")))
            s2 = Srv(d)                                      # "the cloud rebooted"
            try:
                v = s2.view(253, 200, 10, 1)
                self.assertEqual("".join(chr(c & 0xFF) for c in v["cells"][:5]), "hello")
                self.assertGreater(v["tick"], 20)            # continued, did not restart at 0
            finally: s2.stop()

    def test_corrupt_snapshot_falls_back_to_new_world(self):
        with tempfile.TemporaryDirectory() as d:
            open(os.path.join(d, "world.bin"), "wb").write(b"garbage" * 1000)
            s = Srv(d)
            try: self.assertGreater(s.view(0, 0, 8, 8)["wisps"], 10)
            finally: s.stop()

    def test_native_core_longrun_is_stable(self):
        sys.path.insert(0, HERE)
        from server import World
        with tempfile.TemporaryDirectory() as d:
            w = World(d); lo, hi = 10**9, 0
            for i in range(60):
                w.lib.lw_tick(500)                         # 30,000 ticks total
                if i > 3: _, n, _e = w.stats(); lo, hi = min(lo, n), max(hi, n)
            self.assertGreater(lo, 0, "population went extinct")
            self.assertLess(hi, 4096, "population hit the cap")
            print(f"\n  30k ticks on 256x256: wisps min={lo} max={hi}")

if __name__ == "__main__":
    unittest.main(verbosity=2)
