"""Minimal QMP client for the lector simulator."""
import json, socket, time


class Qmp:
    def __init__(self, path):
        for _ in range(100):
            try:
                self.sock = socket.socket(socket.AF_UNIX)
                self.sock.connect(path)
                break
            except (ConnectionRefusedError, FileNotFoundError):
                time.sleep(0.05)
        self.f = self.sock.makefile("rw")
        json.loads(self.f.readline())
        self.cmd("qmp_capabilities")

    def cmd(self, name, **args):
        self.f.write(json.dumps({"execute": name, "arguments": args}) + "\n")
        self.f.flush()
        while True:
            msg = json.loads(self.f.readline())
            if "return" in msg:
                return msg["return"]
            if "error" in msg:
                raise RuntimeError(msg["error"])

    def set(self, path, prop, value):
        return self.cmd("qom-set", path=path, property=prop, value=value)

    def get(self, path, prop):
        return self.cmd("qom-get", path=path, property=prop)
