set(SDK_SRC ${REPO_ROOT}/freeink-sdk/libs/display/FreeInkDisplay/src)

add_executable(BusyWaitIsrTest
  BusyWaitIsrTest.cpp
  ${SDK_SRC}/bus/EpdBus.cpp
)

target_include_directories(BusyWaitIsrTest BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stubs)
target_include_directories(BusyWaitIsrTest SYSTEM PRIVATE ${SDK_SRC}/bus)

target_link_libraries(BusyWaitIsrTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BusyWaitIsrTest)
