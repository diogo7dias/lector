add_executable(CssParserTest
  CssParserTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/css/CssParser.cpp
)

target_include_directories(CssParserTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs  # no-op Logging.h + stdio HalStorage.h (real ones need Arduino/SdFat)
  ${REPO_ROOT}/lib/Epub/Epub/css
  ${REPO_ROOT}/lib/Memory
)

target_link_libraries(CssParserTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(CssParserTest)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME InlineDirectionRegression COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/check_inline_direction.py ${CMAKE_CXX_COMPILER})
