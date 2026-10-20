# Compiles the real UC8253 and UC8279 X3 panel drivers against a recording fake EpdBus, so the
# bus traffic of a refresh (which bank, which plane, in what order) is checkable on the host.
set(SDK_SRC ${REPO_ROOT}/freeink-sdk/libs/display/FreeInkDisplay/src)

add_executable(X3PanelDriverTest
  X3PanelDriverTest.cpp
  ${SDK_SRC}/driver/Uc8253X3Driver.cpp
  ${SDK_SRC}/driver/Uc8253X3Gray.cpp
  ${SDK_SRC}/driver/Uc8279Driver.cpp
)

target_include_directories(X3PanelDriverTest BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stubs)
# SYSTEM: the SDK headers carry -Wextra warnings of their own that are not this test's to fix.
target_include_directories(X3PanelDriverTest SYSTEM PRIVATE ${SDK_SRC}/driver)

target_link_libraries(X3PanelDriverTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(X3PanelDriverTest)
