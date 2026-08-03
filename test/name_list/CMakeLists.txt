add_executable(NameListTest
  NameListTest.cpp
)

target_include_directories(NameListTest PRIVATE
  ${REPO_ROOT}/lib/FsHelpers
  ${REPO_ROOT}/lib/Memory
)

target_link_libraries(NameListTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(NameListTest)
