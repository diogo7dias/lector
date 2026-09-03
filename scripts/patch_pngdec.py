"""
PlatformIO pre-build script:
1. Patch PNGdec's s3_simd_rgb565.S to guard dsps_fft2r_platform.h include.
2. Ensure sdkconfig.<pioenv> exists before checkprogsize/idf_lib_copy.
"""

from pathlib import Path
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


def ensure_env_sdkconfig(*args, **kwargs):
    if not PIOENV:
        return
    sdk_env = PROJECT_DIR / f"sdkconfig.{PIOENV}"
    sdk_defaults = PROJECT_DIR / "sdkconfig.defaults"
    if not sdk_env.exists() and sdk_defaults.exists():
        shutil.copyfile(sdk_defaults, sdk_env)
        print(f"Created {sdk_env.name} from {sdk_defaults.name}")


patch_pngdec()
env.AddPreAction("checkprogsize", ensure_env_sdkconfig)
