add_executable(NearbyFileTransferTest
  NearbyFileRulesTest.cpp
  ${REPO_ROOT}/lib/NearbyFileTransfer/NearbyFileRules.cpp
  ${REPO_ROOT}/src/util/BookFilingNames.cpp
)

target_include_directories(NearbyFileTransferTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(NearbyFileTransferTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(NearbyFileTransferTest)
