add_executable(SpineFileNameIndexTest
  SpineFileNameIndexTest.cpp
)

target_include_directories(SpineFileNameIndexTest PRIVATE
  ${REPO_ROOT}/lib/Epub
)

target_link_libraries(SpineFileNameIndexTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(SpineFileNameIndexTest)
