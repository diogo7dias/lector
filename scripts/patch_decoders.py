"""
PlatformIO pre-build script: apply the image decoder patches via `git apply`.

The upstream JPEGDEC pin still has progressive grayscale bugs in
JPEGDecodeMCU_P, including unsafe MCU_SKIP writes and attempts to consume
chroma components that are absent from the current scan, and both decoder
pins mishandle malformed files (test/fuzz/image). The patches in
`scripts/jpegdec_patches/`, `scripts/pngdec_patches/` and
`scripts/animatedgif_patches/` carry the fixes; this script applies each one against
its libdep working tree.

Each patch's idempotency is decided by git itself:
  * `git apply --check --reverse` succeeds  -> already applied, skip
  * `git apply --check`            succeeds  -> apply
  * neither succeeds                          -> abort the build

Patches live in `scripts/<lib>_patches/` as one-commit-per-fix files
(see the file headers for context). Applied in lexical order.
"""

Import("env")  # noqa: F821 (SCons-injected global)
import os
import subprocess
import sys


# libdep folder name -> patch folder under scripts/
LIBRARIES = {
    "JPEGDEC": "jpegdec_patches",
    "PNGdec": "pngdec_patches",
    "AnimatedGIF": "animatedgif_patches",
}


def patch_decoders(env):
    libdeps_dir = os.path.join(env["PROJECT_DIR"], ".pio", "libdeps")
    if not os.path.isdir(libdeps_dir):
        return
    for lib, patch_folder in LIBRARIES.items():
        patches = _patch_files(os.path.join(env["PROJECT_DIR"], "scripts", patch_folder))
        for env_dir in os.listdir(libdeps_dir):
            lib_dir = os.path.join(libdeps_dir, env_dir, lib)
            if not os.path.isdir(lib_dir):
                continue
            if not os.path.isdir(os.path.join(lib_dir, ".git")):
                # An older registry install: building it would ship the decoder unpatched.
                sys.stderr.write(
                    "ERROR: %s is not a git checkout; delete it so PlatformIO reinstalls it\n"
                    % lib_dir
                )
                raise SystemExit(1)
            for patch in patches:
                _apply_one(lib, lib_dir, patch)


def _patch_files(patch_dir):
    if not os.path.isdir(patch_dir):
        raise RuntimeError(
            "decoder patches missing -- aborting build (expected directory %s)"
            % patch_dir
        )
    patches = sorted(
        os.path.join(patch_dir, name)
        for name in os.listdir(patch_dir)
        if name.endswith(".patch")
    )
    if not patches:
        raise RuntimeError(
            "decoder patches missing -- aborting build (no .patch files in %s)"
            % patch_dir
        )
    return patches


def _apply_one(lib, lib_dir, patch_path):
    name = os.path.basename(patch_path)
    if _git_apply_succeeds(lib_dir, patch_path, reverse=True):
        return
    if not _git_apply_succeeds(lib_dir, patch_path, reverse=False):
        # Not applied, not appliable -- the libdep source has diverged from
        # what the patch expects. Don't write a half-patched file.
        result = subprocess.run(
            ["git", "apply", "--check", patch_path],
            cwd=lib_dir,
            capture_output=True,
            text=True,
        )
        sys.stderr.write(
            "ERROR: %s patch %s does not apply cleanly:\n%s%s\n"
            % (lib, name, result.stdout, result.stderr)
        )
        raise SystemExit(1)
    subprocess.run(["git", "apply", patch_path], cwd=lib_dir, check=True)
    print("Applied %s patch: %s" % (lib, name))


def _git_apply_succeeds(lib_dir, patch_path, *, reverse):
    cmd = ["git", "apply", "--check"]
    if reverse:
        cmd.append("--reverse")
    cmd.append(patch_path)
    return subprocess.run(
        cmd, cwd=lib_dir, capture_output=True, text=True
    ).returncode == 0


patch_decoders(env)  # noqa: F821
