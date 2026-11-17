#!/usr/bin/env python3
"""Per-decoder flash and static RAM from the bench's linker map (after --gc-sections).
Rust bytes are attributed to a crate by object or symbol name; inlined code counts toward its
caller. Merged string sections cannot be attributed and are listed apart; the totals with
strings come from the delta builds in sizes.sh.  Usage: flash.py c3|s3"""
import os
import re
import sys
from collections import defaultdict

BENCH = os.path.dirname(os.path.abspath(__file__))
OUT_KINDS = {".flash.text": "text", ".iram0.text": "iram", ".flash.rodata": "rodata", ".dram0.data": "data",
             ".dram0.bss": "bss"}
RUST_CRATES = ("zune_jpeg", "zune_png", "zune_inflate", "zune_core", "miniz_oxide", "lector_rdec")


def group(path, section):
    if "liblector_rdec.a" in path:
        for crate in RUST_CRATES:
            if crate in section or crate.replace("_", "-") in path.split("(")[-1]:
                return "rust:" + crate
        return "rust:core+alloc+builtins"
    for name, key in (("JPEGDEC", "/JPEGDEC/"), ("PNGdec", "/PNGdec/"), ("tinfl", "/fw_miniz/"),
                      ("uzlib", "/fw_uzlib/"), ("uzlib", "/fw_inflatereader/"), ("bench glue", "/csrc/")):
        if key in path:
            return name
    return None


def main(env):
    sizes = defaultdict(lambda: defaultdict(int))
    out, pending = None, None
    for line in open(os.path.join(BENCH, ".pio", "build", env, "firmware.map"), errors="replace"):
        if re.match(r"^\.\S+", line):
            out = OUT_KINDS.get(line.split()[0])
            continue
        m = re.match(r"^ (\.\S+)\s*$", line)
        if m:
            pending = m.group(1)
            continue
        m = re.match(r"^ (\.\S+)?\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*)$", line)
        if m and out:
            section = m.group(1) or pending or ""
            # Mergeable strings are one merged blob, listed under whichever input came first.
            g = "merged strings (all)" if ".str1." in section else group(m.group(4), section)
            if g and int(m.group(2), 16):
                sizes[g][out] += int(m.group(3), 16)
        pending = None
    print(f"{env}: bytes per output section (text = flash code, iram = code in RAM)")
    for g in sorted(sizes):
        s = sizes[g]
        print(f"  {g:28} text={s['text']:7} iram={s['iram']:5} rodata={s['rodata']:6} data={s['data']:5} "
              f"bss={s['bss']:5} flash_total={s['text'] + s['iram'] + s['rodata'] + s['data']:7}")
    rust = sum(sum(v for k, v in s.items() if k != "bss") for g, s in sizes.items() if g.startswith("rust:"))
    print(f"  rust total flash: {rust}")


if __name__ == "__main__":
    main(sys.argv[1])
