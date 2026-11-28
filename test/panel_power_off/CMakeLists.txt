# Compiles the real Ssd1677Driver and Uc8179Driver against a recording EpdBus to verify
# idle powerOff() drops panel booster rails without breaking the next differential turn.
set(SDK_SRC ${REPO_ROOT}/freeink-sdk/libs/display/FreeInkDisplay/src)

add_executable(PanelPowerOffTest
  PanelPowerOffTest.cpp
  ${SDK_SRC}/driver/Ssd1677Driver.cpp
  ${SDK_SRC}/driver/Uc8179Driver.cpp
)

target_include_directories(PanelPowerOffTest BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stubs)
target_include_directories(PanelPowerOffTest SYSTEM PRIVATE ${SDK_SRC}/driver)

target_link_libraries(PanelPowerOffTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(PanelPowerOffTest)
