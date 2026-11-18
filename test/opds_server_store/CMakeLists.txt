add_executable(OpdsServerStoreTest
  OpdsServerStoreTest.cpp
  ${REPO_ROOT}/src/OpdsServerStore.cpp
  ${REPO_ROOT}/lib/HttpHeader/HttpHeader.cpp
)

target_include_directories(OpdsServerStoreTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/HttpHeader
  ${REPO_ROOT}/lib/AsciiText
)
target_include_directories(OpdsServerStoreTest SYSTEM PRIVATE ${arduinojson_SOURCE_DIR}/src)

target_link_libraries(OpdsServerStoreTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(OpdsServerStoreTest)
