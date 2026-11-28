add_executable(BookMetadataCacheTest
  BookMetadataCacheTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/BookMetadataCache.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(BookMetadataCacheTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/Utf8
)

target_link_libraries(BookMetadataCacheTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BookMetadataCacheTest)

add_executable(ContentOpfSeriesTest
  ContentOpfSeriesTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/ContentOpfParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/BookMetadataCache.cpp
  ${REPO_ROOT}/lib/FsHelpers/FsHelpers.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
)

target_include_directories(ContentOpfSeriesTest BEFORE PRIVATE
  ${REPO_ROOT}/test/fuzz/metadata/stubs
  ${REPO_ROOT}/test/fuzz/common
  ${REPO_ROOT}/test/fs_helpers/stubs
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Epub/Epub
  ${REPO_ROOT}/lib/FsHelpers
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/XmlParserUtils
  ${REPO_ROOT}/lib/expat
)

target_compile_definitions(ContentOpfSeriesTest PRIVATE XML_GE=0 XML_CONTEXT_BYTES=0)

target_link_libraries(ContentOpfSeriesTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ContentOpfSeriesTest)
