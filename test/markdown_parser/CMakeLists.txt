set(FUZZ_CHAPTER ${REPO_ROOT}/test/fuzz/chapter)
add_executable(MarkdownParserTest
  MarkdownParserTest.cpp
  ${FUZZ_CHAPTER}/links.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/MarkdownParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/MarkdownSection.cpp
  ${REPO_ROOT}/lib/Epub/Epub/Page.cpp
  ${REPO_ROOT}/lib/Epub/Epub/ParsedText.cpp
  ${REPO_ROOT}/lib/Epub/Epub/blocks/TextBlock.cpp
  ${REPO_ROOT}/lib/Epub/Epub/blocks/TextBlockFonts.cpp
  ${REPO_ROOT}/lib/Epub/Epub/blocks/TextBlockSerialization.cpp
  ${REPO_ROOT}/lib/FsHelpers/FsHelpers.cpp
  ${REPO_ROOT}/lib/MiniBidi/BidiUtils.cpp
  ${REPO_ROOT}/lib/MiniBidi/minibidi.c
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)
target_include_directories(MarkdownParserTest BEFORE PRIVATE
  ${FUZZ_CHAPTER}/stubs
  ${REPO_ROOT}/test/fuzz/common
  ${REPO_ROOT}/test/fs_helpers/stubs
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Epub/Epub
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/MiniBidi
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/AsciiText
  ${REPO_ROOT}/lib/FsHelpers
)
target_compile_options(MarkdownParserTest PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>)
target_link_libraries(MarkdownParserTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(MarkdownParserTest)
