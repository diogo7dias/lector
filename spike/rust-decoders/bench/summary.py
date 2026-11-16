#!/usr/bin/env python3
"""Pairs each case's C and Rust runs from results/<env>.txt: time ratio, peak heap, stack, and
the runs that died (START with no RESULT, with the allocation that failed).
Usage: summary.py c3|s3"""
import math
import os
import sys
from collections import defaultdict

BENCH = os.path.dirname(os.path.abspath(__file__))
RUST = {"zune-jpeg", "zune-png", "miniz_oxide"}


def main(env):
    lines = open(os.path.join(BENCH, "results", f"{env}.txt")).read().splitlines()
    runs, started, died = defaultdict(dict), {}, []
    for i, ln in enumerate(lines):
        f = ln.split("|")
        if f[0] == "START":
            started = {"name": f[2], "dec": f[3], "free": f[5] if len(f) > 5 else ""}
        elif f[0] == "RESULT":
            name, kind, dec = f[1], f[2], f[3]
            rc, w, h, size, us, peak, stack = int(f[4]), int(f[5]), int(f[6]), int(f[7]), int(f[8]), int(f[10]), int(f[12])
            runs[name][dec] = dict(kind=kind, rc=rc, w=w, h=h, size=size, us=us, peak=peak, stack=stack)
            started = {}
        elif f[0] == "PANIC" and started:
            why = lines[i + 1] if i + 1 < len(lines) and lines[i + 1].startswith("memory") else f[2]
            died.append((started["name"], started["dec"], why, started["free"]))
            runs[started["name"]][started["dec"]] = None
    ratios = defaultdict(list)
    print(f"{env}: case | C us, peak, stack | Rust us, peak, stack | Rust/C time (us = QEMU -icount virtual time)")
    for name, d in runs.items():
        c = next((v for k, v in d.items() if k not in RUST and k != "uzlib"), None)
        r = next((v for k, v in d.items() if k in RUST), "missing")
        cdec = next((k for k in d if k not in RUST and k != "uzlib"), "?")
        if c is None:
            continue
        if not r or r == "missing":
            print(f"  {name[:40]:40} {cdec:7} {c['us']:7} {c['peak']:6} {c['stack']:5} | Rust died")
            continue
        ok = c["rc"] == 0 and r["rc"] == 0
        ratio = r["us"] / c["us"] if ok else float("nan")
        if ok:
            ratios[(c["kind"], cdec)].append(ratio)
        u = d.get("uzlib")
        extra = f" | uzlib {u['us']} {u['peak']} {u['stack']}" if u else ""
        print(f"  {name[:40]:40} {cdec:7} {c['us']:7} {c['peak']:6} {c['stack']:5} | {r['us']:7} {r['peak']:6} "
              f"{r['stack']:5} | {ratio:5.2f} rc {c['rc']}/{r['rc']}{extra}")
    for key, v in ratios.items():
        geo = math.exp(sum(map(math.log, v)) / len(v))
        print(f"  {key}: n={len(v)} Rust/C time geomean={geo:.2f} min={min(v):.2f} max={max(v):.2f}")
    for d in died:
        print(f"  DIED {d[0]} {d[1]}: {d[2]} (heap before: {d[3]})")


if __name__ == "__main__":
    main(sys.argv[1])
