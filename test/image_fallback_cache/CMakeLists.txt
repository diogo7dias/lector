# Lays out a chapter whose image the probe cannot read, on the fuzz_chapter stubs, and
# checks what the fallback extraction leaves on the card.
set(FUZZ_CHAPTER ${REPO_ROOT}/test/fuzz/chapter)
add_executable(ImageFallbackCacheTest
  ImageFallbackCacheTest.cpp
  ${FUZZ_CHAPTER}/links.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageToFramebufferDecoder.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/FootnoteExtractor.cpp
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
target_include_directories(ImageFallbackCacheTest BEFORE PRIVATE
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
target_compile_definitions(ImageFallbackCacheTest PRIVATE XML_GE=0 XML_CONTEXT_BYTES=0)
target_compile_options(ImageFallbackCacheTest PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>)
target_link_libraries(ImageFallbackCacheTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ImageFallbackCacheTest)
