add_executable(TextBlockFontsTest
  TextBlockFontsTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/blocks/TextBlock.cpp
  ${REPO_ROOT}/lib/Epub/Epub/blocks/TextBlockFonts.cpp
  ${REPO_ROOT}/lib/MiniBidi/BidiUtils.cpp
  ${REPO_ROOT}/lib/MiniBidi/minibidi.c
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)
target_include_directories(TextBlockFontsTest PRIVATE
  stubs
  ${REPO_ROOT}/test/book_metadata_cache/stubs
  ${REPO_ROOT}/test/css_parser/stubs
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Epub/Epub
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/MiniBidi
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/Serialization
)
target_compile_options(TextBlockFontsTest PRIVATE -fno-exceptions -fno-rtti)
target_link_libraries(TextBlockFontsTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(TextBlockFontsTest)
