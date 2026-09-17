add_executable(SleepFacePaintTest
  SleepGrayscaleBaseTest.cpp
  SleepPreClearTest.cpp
  SleepFacePaintTest.cpp
)

# stubs/ first: it carries the host HalDisplay (refresh-mode enum only), which shadows the
# real lib/hal one that needs Arduino and the panel driver.
target_include_directories(SleepFacePaintTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/hal
)

target_link_libraries(SleepFacePaintTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(SleepFacePaintTest)
