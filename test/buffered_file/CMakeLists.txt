add_executable(BufferedFileTest
  BufferedFileTest.cpp
)

# Current dir FIRST so <HalStorage.h> resolves to the host stub here (the
# firmware version pulls in SD/Arduino headers that cannot build off-device).
target_include_directories(BufferedFileTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/Memory
)

target_link_libraries(BufferedFileTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BufferedFileTest)
