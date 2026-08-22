add_executable(NearbyFileTransferTest
  NearbyFileRulesTest.cpp
  NearbyFileSessionTest.cpp
  NearbyFilePayloadsTest.cpp
  NearbyFileGroupTest.cpp
  ${REPO_ROOT}/lib/NearbyFileTransfer/NearbyFileRules.cpp
  ${REPO_ROOT}/lib/NearbyFileTransfer/NearbyFileSession.cpp
  ${REPO_ROOT}/lib/NearbyFileTransfer/NearbyFilePayloads.cpp
  ${REPO_ROOT}/lib/NearbyFileTransfer/NearbyFileGroup.cpp
  ${REPO_ROOT}/freeink-sdk/libs/network/NearbyTransfer/src/NearbyTransfer.cpp
  ${REPO_ROOT}/src/util/BookFilingNames.cpp
)

target_include_directories(NearbyFileTransferTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/freeink-sdk/libs/network/NearbyTransfer/include
)

target_link_libraries(NearbyFileTransferTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(NearbyFileTransferTest)
