# Links the system wolfSSL (libwolfssl-dev): the device verifies with the same
# wolfCrypt calls, so this checks the real verify path, not a stand-in.
find_package(PkgConfig REQUIRED)
pkg_check_modules(WOLFSSL REQUIRED IMPORTED_TARGET wolfssl)

add_executable(FirmwareSignatureTest
  FirmwareSignatureTest.cpp
  ${REPO_ROOT}/src/network/FirmwareSignature.cpp
)

target_include_directories(FirmwareSignatureTest PRIVATE
  ${REPO_ROOT}/src/network
  ${REPO_ROOT}/lib/Memory
)

# The distro build is configured by options.h; the device build by user_settings.h.
target_compile_options(FirmwareSignatureTest PRIVATE -include wolfssl/options.h)

target_link_libraries(FirmwareSignatureTest PRIVATE
  crosspoint_test_common
  PkgConfig::WOLFSSL
  GTest::gtest_main
)

gtest_discover_tests(FirmwareSignatureTest)
