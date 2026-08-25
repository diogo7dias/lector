add_executable(LightPanelGeometryTest
  LightPanelGeometryTest.cpp
)

target_include_directories(LightPanelGeometryTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(LightPanelGeometryTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(LightPanelGeometryTest)
