add_executable(CredentialBundleTest
  CredentialBundleTest.cpp
  ${REPO_ROOT}/src/util/CredentialBundle.cpp
)

target_include_directories(CredentialBundleTest PRIVATE
  ${REPO_ROOT}/src/util
)

target_link_libraries(CredentialBundleTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(CredentialBundleTest)
