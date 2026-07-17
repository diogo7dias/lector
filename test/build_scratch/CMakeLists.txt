add_executable(build_scratch_tests
  BuildScratchTest.cpp
  ${REPO_ROOT}/lib/Memory/BuildScratch.cpp
)

target_link_libraries(build_scratch_tests PRIVATE crosspoint_test_common GTest::gtest_main)
# The current dir is listed first so <Logging.h> resolves to the host stub here
# (the firmware Logging.h pulls in <HardwareSerial.h> and cannot build off-device).
target_include_directories(build_scratch_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${REPO_ROOT}/lib/Memory
)

gtest_discover_tests(build_scratch_tests)
