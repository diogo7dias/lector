find_package(EXPAT REQUIRED)

add_executable(OpdsFeedsTest
  OpdsFeedsTest.cpp
  ${REPO_ROOT}/lib/OpdsParser/OpdsParser.cpp
  ${REPO_ROOT}/src/util/UrlUtils.cpp
)

# stubs/ first so <Print.h> and <Logging.h> resolve to the host stubs.
target_include_directories(OpdsFeedsTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${REPO_ROOT}/lib/OpdsParser
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/XmlParserUtils
  ${REPO_ROOT}/src
)

target_link_libraries(OpdsFeedsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
  EXPAT::EXPAT
)

gtest_discover_tests(OpdsFeedsTest)
