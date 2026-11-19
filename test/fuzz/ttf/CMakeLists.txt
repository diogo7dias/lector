# SD-card TTF/OTF fonts (X4 Pro): the metrics reader in TtfFamilyScan.h and the stb_truetype
# TtfFont the firmware compiles from the FreeInk SDK, driven the way TtfSdFont drives them.
#
# The firmware applies scripts/stb_truetype_patches to the SDK sources before it builds
# (scripts/patch_decoders.py); this target applies the same patches to copies in the build tree
# so the submodule checkout stays untouched, and refuses to configure if one does not apply.
set(FREEINK_BOOK ${REPO_ROOT}/freeink-sdk/libs/book/FreeInkBook)
set(TTF_ROOT ${CMAKE_CURRENT_BINARY_DIR}/sdk)
set(TTF_SRC ${TTF_ROOT}/libs/book/FreeInkBook)
file(GLOB TTF_PATCHES ${REPO_ROOT}/scripts/stb_truetype_patches/*.patch)
list(SORT TTF_PATCHES)
if(NOT TTF_PATCHES)
  message(FATAL_ERROR "scripts/stb_truetype_patches is empty: fuzz_ttf would test the unpatched stb")
endif()

file(MAKE_DIRECTORY ${TTF_SRC}/src/render ${TTF_SRC}/third_party/stb)
foreach(rel src/render/TtfFont.cpp third_party/stb/stb_truetype.h)
  execute_process(COMMAND ${CMAKE_COMMAND} -E copy ${FREEINK_BOOK}/${rel} ${TTF_SRC}/${rel})
endforeach()
# The copies get a repository of their own: inside the build tree git would otherwise resolve the
# patch paths against the enclosing checkout and skip them without an error.
execute_process(COMMAND git init -q ${TTF_ROOT})
foreach(patch ${TTF_PATCHES})
  execute_process(COMMAND git apply ${patch} WORKING_DIRECTORY ${TTF_ROOT}
    RESULT_VARIABLE rc ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "${patch} does not apply to the SDK sources:\n${err}")
  endif()
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${TTF_PATCHES}
  ${FREEINK_BOOK}/src/render/TtfFont.cpp ${FREEINK_BOOK}/third_party/stb/stb_truetype.h)

lector_add_fuzzer(ttf
  fuzz_ttf.cpp
  ${TTF_SRC}/src/render/TtfFont.cpp
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
  ${TTF_SRC}/third_party/stb
)
