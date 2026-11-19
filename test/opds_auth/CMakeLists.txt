add_executable(OpdsAuthTest
  OpdsAuthTest.cpp
  ${REPO_ROOT}/lib/OpdsAuth/OpdsAuth.cpp
)

target_include_directories(OpdsAuthTest PRIVATE
  ${REPO_ROOT}/lib/OpdsAuth
)
target_include_directories(OpdsAuthTest SYSTEM PRIVATE ${arduinojson_SOURCE_DIR}/src)

target_link_libraries(OpdsAuthTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(OpdsAuthTest)
