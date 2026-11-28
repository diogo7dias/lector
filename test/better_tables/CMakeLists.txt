set(FUZZ_CHAPTER ${REPO_ROOT}/test/fuzz/chapter)
set(TABLE_TEST_SOURCES
  BetterTablesTest.cpp
  ${FUZZ_CHAPTER}/links.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageToFramebufferDecoder.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/VoidTagFixer.cpp
  ${REPO_ROOT}/lib/Epub/Epub/Page.cpp
  ${REPO_ROOT}/lib/Epub/Epub/ParsedText.cpp
  ${REPO_ROOT}/lib/Epub/Epub/blocks/TextBlock.cpp
  ${REPO_ROOT}/lib/Epub/Epub/blocks/TextBlockFonts.cpp
  ${REPO_ROOT}/lib/Epub/Epub/blocks/TextBlockSerialization.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageDimsProbe.cpp
  ${REPO_ROOT}/lib/Epub/Epub/css/CssParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/htmlEntities.cpp
  ${REPO_ROOT}/lib/FsHelpers/FsHelpers.cpp
  ${REPO_ROOT}/lib/MiniBidi/BidiUtils.cpp
  ${REPO_ROOT}/lib/MiniBidi/minibidi.c
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
)
set(TABLE_TEST_INCLUDES
  ${FUZZ_CHAPTER}/stubs
  ${REPO_ROOT}/test/fuzz/common
  ${REPO_ROOT}/test/css_parser/stubs
  ${REPO_ROOT}/test/fs_helpers/stubs
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Epub/Epub
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/MiniBidi
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/AsciiText
  ${REPO_ROOT}/lib/FsHelpers
  ${REPO_ROOT}/lib/XmlParserUtils
  ${REPO_ROOT}/lib/expat
)

add_executable(BetterTablesTest ${TABLE_TEST_SOURCES})
target_include_directories(BetterTablesTest BEFORE PRIVATE ${TABLE_TEST_INCLUDES})
target_compile_definitions(BetterTablesTest PRIVATE XML_GE=0 XML_CONTEXT_BYTES=0)
target_compile_options(BetterTablesTest PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>)
target_link_libraries(BetterTablesTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BetterTablesTest)

add_executable(BetterTablesPsramTest ${TABLE_TEST_SOURCES})
target_include_directories(BetterTablesPsramTest BEFORE PRIVATE ${TABLE_TEST_INCLUDES})
target_compile_definitions(BetterTablesPsramTest PRIVATE XML_GE=0 XML_CONTEXT_BYTES=0 BOARD_HAS_PSRAM=1)
target_compile_options(BetterTablesPsramTest PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>)
target_link_libraries(BetterTablesPsramTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BetterTablesPsramTest TEST_PREFIX "Psram.")
