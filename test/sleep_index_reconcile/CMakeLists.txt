add_executable(SleepIndexReconcileTest
  SleepIndexReconcileTest.cpp
  ${REPO_ROOT}/src/util/FavoriteImageNames.cpp
)

target_include_directories(SleepIndexReconcileTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(SleepIndexReconcileTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(SleepIndexReconcileTest)
