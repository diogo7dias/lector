"""PlatformIO pre-build hook: compile and link the Rust fshelpers staticlib.

Opt-in only. Does nothing unless the build defines USE_RUST_FSHELPERS (add
`-DUSE_RUST_FSHELPERS` to build_flags, e.g. in platformio.local.ini). When the
flag is absent the normal C++ firmware builds exactly as before.

When enabled it:
  1. runs `cargo build --release --features device --target riscv32imc-unknown-none-elf`
  2. adds the crate's include/ dir to CPPPATH (for fshelpers_rs.h)
  3. links the resulting libfshelpers_rs.a into the firmware
"""

import os
import shutil
import subprocess
import sys

Import("env")  # noqa: F821 - injected by PlatformIO/SCons

RUST_CRATE_DIR = os.path.join(env.subst("$PROJECT_DIR"), "rust", "fshelpers-rs")  # noqa: F821
RUST_TARGET = "riscv32imc-unknown-none-elf"
STATICLIB = "libfshelpers_rs.a"
DEFINE = "USE_RUST_FSHELPERS"


def _defined(name):
    for d in env.get("CPPDEFINES", []):  # noqa: F821
        key = d[0] if isinstance(d, (list, tuple)) else d
        if key == name:
            return True
    # Fallback: scan raw build_flags in case flags aren't parsed into CPPDEFINES yet.
    return any(name in str(f) for f in env.get("BUILD_FLAGS", []))  # noqa: F821


def _cargo():
    exe = shutil.which("cargo")
    if exe:
        return exe
    home = os.path.expanduser("~")
    candidate = os.path.join(home, ".cargo", "bin", "cargo")
    return candidate if os.path.exists(candidate) else None


if not _defined(DEFINE):
    print("[build_rust] %s not defined -> skipping Rust build (C++ path)." % DEFINE)
else:
    cargo = _cargo()
    if not cargo:
        sys.exit("[build_rust] ERROR: %s set but `cargo` not found. Install Rust "
                 "(https://rustup.rs) and add the %s target." % (DEFINE, RUST_TARGET))

    print("[build_rust] building Rust staticlib for %s ..." % RUST_TARGET)
    result = subprocess.run(
        [cargo, "build", "--release", "--features", "device", "--target", RUST_TARGET],
        cwd=RUST_CRATE_DIR,
    )
    if result.returncode != 0:
        sys.exit("[build_rust] ERROR: cargo build failed (rc=%d)." % result.returncode)

    lib_path = os.path.join(RUST_CRATE_DIR, "target", RUST_TARGET, "release", STATICLIB)
    if not os.path.exists(lib_path):
        sys.exit("[build_rust] ERROR: expected artifact missing: %s" % lib_path)

    include_dir = os.path.join(RUST_CRATE_DIR, "include")
    env.Append(CPPPATH=[include_dir])          # noqa: F821 - so C++ finds fshelpers_rs.h
    # LIBS (not LINKFLAGS): SCons places these AFTER the object files on the link
    # line, so the archive still resolves symbols referenced by FsHelpers.cpp.o.
    # A static archive placed before its callers gets its members discarded.
    env.Append(LIBS=[env.File(lib_path)])      # noqa: F821

    print("[build_rust] will link %s" % lib_path)
