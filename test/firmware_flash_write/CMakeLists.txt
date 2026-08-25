add_executable(FirmwareFlashWriteTest
  FirmwareFlashWriteTest.cpp
  ${REPO_ROOT}/src/network/FlashWriteVerify.cpp
)

target_include_directories(FirmwareFlashWriteTest PRIVATE
  ${REPO_ROOT}/src/network
)

target_link_libraries(FirmwareFlashWriteTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(FirmwareFlashWriteTest)
