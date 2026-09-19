add_executable(FontManifestParserTest
  FontManifestParserTest.cpp
  ${REPO_ROOT}/lib/JsonParser/FontManifestParser.cpp
  ${REPO_ROOT}/lib/JsonParser/StreamingJsonParser.cpp
)

target_include_directories(FontManifestParserTest PRIVATE
  ${REPO_ROOT}/lib/JsonParser
  ${REPO_ROOT}/lib/Memory
)

# The published fonts.json, parsed as a fixture: see the "Published" suite.
target_compile_definitions(FontManifestParserTest PRIVATE
  PUBLISHED_FONTS_JSON="${CMAKE_CURRENT_SOURCE_DIR}/published_fonts.json"
)

target_link_libraries(FontManifestParserTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(FontManifestParserTest)
