add_executable(WordSpacingTest
  WordSpacingTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/ParsedText.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/Hyphenator.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/LanguageRegistry.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/LiangHyphenation.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/HyphenationCommon.cpp
  ${REPO_ROOT}/lib/MiniBidi/BidiUtils.cpp
  ${REPO_ROOT}/lib/MiniBidi/minibidi.c
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)
target_include_directories(WordSpacingTest PRIVATE
  stubs
  ${REPO_ROOT}/test/css_parser/stubs
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Epub/Epub
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/MiniBidi
  ${REPO_ROOT}/lib/Utf8
)
target_compile_definitions(WordSpacingTest PRIVATE
  WORD_SPACING_BASELINE="${CMAKE_CURRENT_SOURCE_DIR}/baseline.txt"
)
target_compile_options(WordSpacingTest PRIVATE -fno-exceptions -fno-rtti)
target_link_libraries(WordSpacingTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(WordSpacingTest)
