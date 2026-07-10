add_executable(SerializationBoundsTest
  SerializationBoundsTest.cpp
)

target_include_directories(SerializationBoundsTest PRIVATE
  ${REPO_ROOT}/lib/Serialization
)

target_link_libraries(SerializationBoundsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(SerializationBoundsTest)
