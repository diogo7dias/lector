add_executable(StatusScreenAuditTest
  StatusScreenAuditTest.cpp
)

target_compile_definitions(StatusScreenAuditTest PRIVATE
  STATUS_SCREEN_SOURCES="${REPO_ROOT}/src/activities/settings/ClearCacheActivity.cpp|${REPO_ROOT}/src/activities/settings/CleanStorageActivity.cpp|${REPO_ROOT}/src/activities/settings/ClockSyncActivity.cpp|${REPO_ROOT}/src/activities/settings/KOReaderAuthActivity.cpp|${REPO_ROOT}/src/activities/network/CalibreConnectActivity.cpp|${REPO_ROOT}/src/activities/network/CrossPointWebServerActivity.cpp|${REPO_ROOT}/src/activities/settings/OtaUpdateActivity.cpp|${REPO_ROOT}/src/activities/settings/SdFirmwareUpdateActivity.cpp"
)

target_link_libraries(StatusScreenAuditTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(StatusScreenAuditTest)
