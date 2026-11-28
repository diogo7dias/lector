add_executable(TxtJustifyTest TxtJustifyTest.cpp)
target_include_directories(TxtJustifyTest PRIVATE
  ${REPO_ROOT}/src/activities/reader
  ${REPO_ROOT}/lib/Epub
)
target_link_libraries(TxtJustifyTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(TxtJustifyTest)
