add_executable(TlsLoanAuditTest
  TlsLoanAuditTest.cpp
)

target_compile_definitions(TlsLoanAuditTest PRIVATE
  TLS_LOAN_AUDIT_SOURCES="${REPO_ROOT}/src/activities/settings/FontDownloadActivity.cpp"
)

target_link_libraries(TlsLoanAuditTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(TlsLoanAuditTest)
