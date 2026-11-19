add_executable(OpdsTokenStoreTest
  OpdsTokenStoreTest.cpp
  ${REPO_ROOT}/src/OpdsTokenStore.cpp
)

# The stand-ins for the card and the device key are shared with opds_server_store.
target_include_directories(OpdsTokenStoreTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../opds_server_store/stubs
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/OpdsAuth
)
target_include_directories(OpdsTokenStoreTest SYSTEM PRIVATE ${arduinojson_SOURCE_DIR}/src)

target_link_libraries(OpdsTokenStoreTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(OpdsTokenStoreTest)
