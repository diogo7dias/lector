# Links the system wolfSSL (libwolfssl-dev), like firmware_signature.
find_package(PkgConfig REQUIRED)
pkg_check_modules(WOLFSSL REQUIRED IMPORTED_TARGET wolfssl)

add_executable(CertPinTest CertPinTest.cpp)

target_include_directories(CertPinTest PRIVATE
  ${REPO_ROOT}/freeink-sdk/libs/network/SecureNet/include
)

target_compile_options(CertPinTest PRIVATE -include wolfssl/options.h)

target_link_libraries(CertPinTest PRIVATE
  crosspoint_test_common
  PkgConfig::WOLFSSL
  GTest::gtest_main
)

gtest_discover_tests(CertPinTest)
