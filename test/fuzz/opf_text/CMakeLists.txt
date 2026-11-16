# OPF text clean-up from Epub::parseContentOpf: description flattening, NFC composition and
# the guide cover-page image scan.
lector_add_fuzzer(opf_text
  fuzz_opf_text.cpp
  ${REPO_ROOT}/lib/Epub/Epub/OpfText.cpp
  ${REPO_ROOT}/lib/FsHelpers/FsHelpers.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(fuzz_opf_text BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../image/stubs
  ${CMAKE_CURRENT_SOURCE_DIR}/../common
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/FsHelpers
  ${REPO_ROOT}/lib/Utf8
)
