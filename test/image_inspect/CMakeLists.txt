add_executable(ImageInspectLayoutTest
  ImageInspectLayoutTest.cpp
)

target_include_directories(ImageInspectLayoutTest PRIVATE ${REPO_ROOT})

target_link_libraries(ImageInspectLayoutTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ImageInspectLayoutTest)
