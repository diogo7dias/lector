add_executable(TrustedPinsTest TrustedPinsTest.cpp)

target_include_directories(TrustedPinsTest PRIVATE
  ${REPO_ROOT}/lib/TrustedRoots
)

target_link_libraries(TrustedPinsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(TrustedPinsTest)
