add_executable(DeviceLookAuditTest
  DeviceLookAuditTest.cpp
)

target_compile_definitions(DeviceLookAuditTest PRIVATE
  TOUCH_SHAPE_SOURCES="${REPO_ROOT}/src/activities/UiStatusActivity.cpp|${REPO_ROOT}/src/activities/UiGridActivity.cpp"
)

target_link_libraries(DeviceLookAuditTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(DeviceLookAuditTest)
