from pathlib import Path

Import("env")


PROJECT_DIR = Path(env.subst("$PROJECT_DIR"))
MARKER = "/* CrossPoint wolfSSL compatibility overrides */"
OVERRIDES = f"""

{MARKER}
#undef NO_DH
#ifndef HAVE_FFDHE_2048
#define HAVE_FFDHE_2048
#endif
/* 8192 supports RSA-4096 verification (fp_mul products are 2x the modulus;
 * Let's Encrypt's ISRG Root X1 is RSA-4096) plus FFDHE-2048. The previous
 * 16384 made every WOLFSSL_SMALL_STACK fastmath temporary a ~2KB heap
 * allocation during the handshake -- the most fragmentation-sensitive moment
 * on this device; 8192 halves those temporaries. */
#undef FP_MAX_BITS
#define FP_MAX_BITS 8192
"""


def patch_user_settings(path: Path) -> None:
    text = path.read_text()
    if MARKER in text:
        text = text.split(MARKER, 1)[0].rstrip()
    path.write_text(text + OVERRIDES + "\n")
    print(f"Patched wolfSSL settings: {path.relative_to(PROJECT_DIR)}")


for settings in PROJECT_DIR.glob(".pio/libdeps/*/Arduino-wolfSSL/src/user_settings.h"):
    patch_user_settings(settings)
