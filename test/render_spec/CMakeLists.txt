add_executable(RenderSpecTest
  RenderSpecTest.cpp
)

target_include_directories(RenderSpecTest PRIVATE
  ${REPO_ROOT}/src/activities/reader
  ${REPO_ROOT}/lib/Epub
)

target_link_libraries(RenderSpecTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(RenderSpecTest)
