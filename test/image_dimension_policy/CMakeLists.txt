add_executable(ImageDimensionPolicyTest
  ImageDimensionPolicyTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageToFramebufferDecoder.cpp
)

# Current dir FIRST so <Logging.h> and <HalStorage.h> resolve to the host
# stubs here (the firmware versions pull in SD/Arduino headers that cannot
# build off-device).
target_include_directories(ImageDimensionPolicyTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${REPO_ROOT}/lib/Epub/Epub/converters
)

target_link_libraries(ImageDimensionPolicyTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ImageDimensionPolicyTest)
