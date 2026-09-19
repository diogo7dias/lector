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

if(UNIX)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_test(NAME TlsSocketReceive COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/socket_receive_test.py ${CMAKE_CXX_COMPILER})
endif()
