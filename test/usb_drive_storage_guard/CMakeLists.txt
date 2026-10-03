add_executable(UsbDriveStorageGuardTest
  UsbDriveStorageGuardTest.cpp
  ${REPO_ROOT}/lib/hal/HalStorage.cpp
)

target_include_directories(UsbDriveStorageGuardTest BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/hal
  ${REPO_ROOT}/lib/Memory
)

target_compile_definitions(UsbDriveStorageGuardTest PRIVATE FREEINK_SD_SDMMC=1 FREEINK_CAP_USB_MSC=1)

target_link_libraries(UsbDriveStorageGuardTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(UsbDriveStorageGuardTest)
