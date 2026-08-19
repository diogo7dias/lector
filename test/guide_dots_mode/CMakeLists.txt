add_executable(GuideDotsModeTest
  GuideDotsModeTest.cpp
)

target_include_directories(GuideDotsModeTest PRIVATE
  ${REPO_ROOT}/lib/Epub
)

target_link_libraries(GuideDotsModeTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(GuideDotsModeTest)
