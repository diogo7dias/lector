add_executable(inflate_stream_tests
  InflateStreamTest.cpp
  ${REPO_ROOT}/lib/miniz/src/InflateStream.cpp
  ${REPO_ROOT}/lib/miniz/src/miniz_impl.c
  ${REPO_ROOT}/lib/Memory/BuildScratch.cpp
)

target_link_libraries(inflate_stream_tests PRIVATE crosspoint_test_common GTest::gtest_main)
# The current dir is listed first so <Logging.h> resolves to the host stub here
# (the firmware Logging.h pulls in <HardwareSerial.h> and cannot build off-device).
target_include_directories(inflate_stream_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${REPO_ROOT}/lib/miniz/src
  ${REPO_ROOT}/lib/Memory
)

gtest_discover_tests(inflate_stream_tests)
