add_executable(NearbyPositionSyncTest
  NearbyPositionProtocolTest.cpp
  NearbyPositionSessionTest.cpp
  ${REPO_ROOT}/lib/NearbyPositionSync/NearbyPositionProtocol.cpp
  ${REPO_ROOT}/lib/NearbyPositionSync/NearbyPositionSession.cpp
)

target_link_libraries(NearbyPositionSyncTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(NearbyPositionSyncTest)
