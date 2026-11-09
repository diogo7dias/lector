add_executable(AnchorPlacementTest AnchorPlacementTest.cpp)

target_include_directories(AnchorPlacementTest PRIVATE ${REPO_ROOT}/lib/Epub/Epub/parsers)

target_link_libraries(AnchorPlacementTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(AnchorPlacementTest)
