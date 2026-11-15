# CssParser (EPUB stylesheets are untrusted input). Stubs and include dirs mirror test/css_parser.
lector_add_fuzzer(css
  fuzz_css.cpp
  ${REPO_ROOT}/lib/Epub/Epub/css/CssParser.cpp
)

target_include_directories(fuzz_css PRIVATE
  ${REPO_ROOT}/test/css_parser/stubs
  ${REPO_ROOT}/lib/Epub/Epub/css
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/AsciiText
  ${REPO_ROOT}/lib/Memory
)
