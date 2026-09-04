"""
PlatformIO pre-build script:
1. Patch PNGdec's s3_simd_rgb565.S to guard dsps_fft2r_platform.h include.
2. Synchronize sdkconfig.defaults TASMOTA hash with target MCU to prevent
   unnecessary framework reinstall loops between C3 and S3.
3. Ensure build output directories exist to prevent GCC .d file creation race.
4. Ensure sdkconfig.<pioenv> exists before checkprogsize/idf_lib_copy.
"""

from pathlib import Path
import hashlib
import shutil

Import("env")

PROJECT_DIR = Path(env.subst("$PROJECT_DIR"))
PIOENV = env.get("PIOENV", "")


def patch_pngdec():
    target = '#include "dsps_fft2r_platform.h"'
    replacement = """#if __has_include("dsps_fft2r_platform.h")
#include "dsps_fft2r_platform.h"
#else
#define dsps_fft2r_sc16_aes3_enabled 1
#endif"""

    for s_file in PROJECT_DIR.glob(".pio/libdeps/*/PNGdec/src/s3_simd_rgb565.S"):
        try:
            content = s_file.read_text()
            if target in content:
                s_file.write_text(content.replace(target, replacement, 1))
                print(f"Patched PNGdec S3 SIMD: {s_file.relative_to(PROJECT_DIR)}")
        except Exception as e:
            print(f"Warning: could not patch {s_file}: {e}")


def sync_sdkconfig_defaults():
    sdk_defaults = PROJECT_DIR / "sdkconfig.defaults"
    if not sdk_defaults.exists():
        return
    try:
        board = env.BoardConfig()
        mcu = board.get("build.mcu", "esp32")
        try:
            custom_options = env.GetProjectOption("custom_sdkconfig")
        except Exception:
            custom_options = ""
        expected_hash = hashlib.md5((custom_options.strip() + mcu).encode("utf-8")).hexdigest()[:16]
        expected_header = f"# TASMOTA__{expected_hash}\n"

        lines = sdk_defaults.read_text().splitlines(keepends=True)
        if not lines or not lines[0].startswith("# TASMOTA__") or lines[0] != expected_header:
            if lines and lines[0].startswith("# TASMOTA__"):
                lines[0] = expected_header
            else:
                lines.insert(0, expected_header)
            sdk_defaults.write_text("".join(lines))
    except Exception as e:
        print(f"Warning: could not sync sdkconfig.defaults: {e}")


def ensure_build_directories():
    if not PIOENV:
        return
    build_src = PROJECT_DIR / f".pio/build/{PIOENV}/src"
    for d in ["", "activities", "activities/boot_sleep", "activities/browser", "network", "util"]:
        (build_src / d).mkdir(parents=True, exist_ok=True)


def ensure_env_sdkconfig(*args, **kwargs):
    if not PIOENV:
        return
    sdk_env = PROJECT_DIR / f"sdkconfig.{PIOENV}"
    sdk_defaults = PROJECT_DIR / "sdkconfig.defaults"
    if not sdk_env.exists() and sdk_defaults.exists():
        shutil.copyfile(sdk_defaults, sdk_env)
        print(f"Created {sdk_env.name} from {sdk_defaults.name}")


patch_pngdec()
sync_sdkconfig_defaults()
ensure_build_directories()
env.AddPreAction("checkprogsize", ensure_env_sdkconfig)
