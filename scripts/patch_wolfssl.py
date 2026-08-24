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
/* MEMFIX-PORT: 8192 handles up to RSA-4096 keys (the public-CA maximum,
   ISRG Root X1 included) with half the per-bignum heap of 16384: with
   WOLFSSL_SMALL_STACK each fast-math temp is FP_MAX_BITS/8 * 2 bytes on the
   heap, and TLS cert verification allocates dozens at once. */
#undef FP_MAX_BITS
#define FP_MAX_BITS 8192
"""


def patch_user_settings(path: Path) -> None:
    text = path.read_text()
    if MARKER in text:
        text = text.split(MARKER, 1)[0].rstrip()
    path.write_text(text + OVERRIDES + "\n")
    print(f"Patched wolfSSL settings: {path.relative_to(PROJECT_DIR)}")


# wolfSSL frees the dynamic input buffer after every record it finishes and falls
# back to the 5-byte static one, so each new record reallocates. Peers that ignore
# our max_fragment_length request (release-assets.githubusercontent.com does) send
# 16 KB records, which means a ~16.6 KB contiguous allocation per record. The first
# one succeeds; by the second the heap has fragmented and wolfSSL returns -125
# MEMORY_E part-way through the body.
#
# ShrinkInputBuffer does two jobs at once, though: it frees the buffer AND it
# reclaims the space already consumed, by copying the unread tail to the front and
# resetting idx. Skipping it entirely keeps the buffer but never reclaims, so
# wolfSSL grows it by a whole record each time and dies one record later than
# before. So keep the reclaim and drop only the free: compact the buffer in place
# and leave it allocated at its current size. FORCED_FREE still frees it in
# SSL_ResourceFree, so nothing leaks.
SHRINK_MARKER = "/* CrossPoint: compact the dynamic input buffer, do not free it */"
SHRINK_ANCHOR = """void ShrinkInputBuffer(WOLFSSL* ssl, int forcedFree)
{
"""
SHRINK_PATCH = (
    SHRINK_ANCHOR
    + f"""    {SHRINK_MARKER}
    if (!forcedFree && ssl->buffers.inputBuffer.dynamicFlag) {{
        int keptLength = (int)(ssl->buffers.inputBuffer.length -
                               ssl->buffers.inputBuffer.idx);
        if (keptLength > 0)
            XMEMMOVE(ssl->buffers.inputBuffer.buffer,
                     ssl->buffers.inputBuffer.buffer + ssl->buffers.inputBuffer.idx,
                     (size_t)keptLength);
        ssl->buffers.inputBuffer.idx = 0;
        ssl->buffers.inputBuffer.length = (word32)keptLength;
        return;
    }}

"""
)


def patch_internal_c(path: Path) -> None:
    text = path.read_text()
    if SHRINK_MARKER in text:
        return
    if SHRINK_ANCHOR not in text:
        raise SystemExit(f"wolfSSL patch: ShrinkInputBuffer not found in {path}")
    path.write_text(text.replace(SHRINK_ANCHOR, SHRINK_PATCH, 1))
    print(f"Patched wolfSSL input buffer shrink: {path.relative_to(PROJECT_DIR)}")


for settings in PROJECT_DIR.glob(".pio/libdeps/*/Arduino-wolfSSL/src/user_settings.h"):
    patch_user_settings(settings)

for internal in PROJECT_DIR.glob(".pio/libdeps/*/Arduino-wolfSSL/src/src/internal.c"):
    patch_internal_c(internal)
