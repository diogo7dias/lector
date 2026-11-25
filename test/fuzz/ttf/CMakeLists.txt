# SD-card TTF/OTF fonts (X4 Pro): the metrics reader in TtfFamilyScan.h and the stb_truetype
# TtfFont the firmware compiles from the FreeInk SDK, driven the way TtfSdFont drives them.
# The SDK fork carries the bounded reads (stbtt_BindData) as commits, so this builds its sources
# as they are checked out, like the firmware does.
set(FREEINK_BOOK ${REPO_ROOT}/freeink-sdk/libs/book/FreeInkBook)

lector_add_fuzzer(ttf
  fuzz_ttf.cpp
  ${FREEINK_BOOK}/src/render/TtfFont.cpp
)

# -fwrapv and the allocation cap come from StbTtfPrelude.h in the firmware; clang ignores the GCC
# pragma, so the flag is set here. float-cast-overflow is off: TtfFont and stb turn a float into
# int16/int with a plain cast, and a hostile font can make the value too big (a 65535-unit
# advance in a 16-unit em). On both target ISAs the conversion saturates or truncates without
# trapping, so the cost is one wrong advance or glyph size, never a stray memory access. Every
# other UBSan check stays on.
target_compile_options(fuzz_ttf PRIVATE -fwrapv -fno-sanitize=float-cast-overflow
  -include ${REPO_ROOT}/lib/EpdFont/StbTtfPrelude.h)
target_include_directories(fuzz_ttf PRIVATE
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Serialization
  ${FREEINK_BOOK}/include
  ${FREEINK_BOOK}/third_party/stb
)
