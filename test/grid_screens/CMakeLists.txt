add_executable(GridScreenAuditTest
  GridScreenAuditTest.cpp
  GridRowsTest.cpp
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp
)

target_compile_definitions(GridScreenAuditTest PRIVATE
  GRID_SCREEN_SOURCES="${REPO_ROOT}/src/activities/settings/SettingsActivity.cpp|${REPO_ROOT}/src/activities/settings/TextSettingsActivity.cpp"
)

target_link_libraries(GridScreenAuditTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(GridScreenAuditTest)

# Compile the production grid/host/theme bodies against host adapters. Only include
# directives are removed: the firmware headers pull in Arduino/FreeRTOS. No UI,
# selection, routing, or gate logic is copied into the test.
foreach(source IN ITEMS activities/UiGridActivity.h activities/UiGridActivity.cpp
                        components/UiAppHost.h components/UiAppHost.cpp components/UIThemeTokens.h)
  set(path "${REPO_ROOT}/src/${source}")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")
  file(READ "${path}" body)
  string(REGEX REPLACE "#include[^\n]*" "" body "${body}")
  get_filename_component(name "${source}" NAME)
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${name}.inc" "${body}")
endforeach()
target_include_directories(GridScreenAuditTest PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}
  ${REPO_ROOT}/src
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/include
)
