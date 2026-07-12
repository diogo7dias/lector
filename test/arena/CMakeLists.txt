add_executable(arena_tests
  ArenaTest.cpp
)

target_link_libraries(arena_tests PRIVATE crosspoint_test_common GTest::gtest_main)
# The current dir is listed first so <Logging.h> resolves to the host stub here
# (the firmware Logging.h pulls in <HardwareSerial.h> and cannot build off-device).
target_include_directories(arena_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${REPO_ROOT}/lib/Memory
)

gtest_discover_tests(arena_tests)
