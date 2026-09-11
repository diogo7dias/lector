add_executable(FirmwareRetryFlowTest
  FirmwareRetryFlowTest.cpp
)

target_compile_definitions(FirmwareRetryFlowTest PRIVATE
  OTA_ACTIVITY_SOURCE="${REPO_ROOT}/src/activities/settings/OtaUpdateActivity.cpp"
  SD_ACTIVITY_SOURCE="${REPO_ROOT}/src/activities/settings/SdFirmwareUpdateActivity.cpp"
  OTA_UPDATER_SOURCE="${REPO_ROOT}/src/network/OtaUpdater.cpp"
  FLASHER_SOURCE="${REPO_ROOT}/src/network/FirmwareFlasher.cpp"
  DIAGNOSTICS_SOURCE="${REPO_ROOT}/src/network/FlashDiagnostics.cpp"
)

target_link_libraries(FirmwareRetryFlowTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(FirmwareRetryFlowTest)
