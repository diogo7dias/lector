add_executable(AsyncPageTurnTest
  AsyncPageTurnTest.cpp
  ${REPO_ROOT}/lib/hal/HalDisplay.cpp
)

target_include_directories(AsyncPageTurnTest BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stubs)
target_include_directories(AsyncPageTurnTest PRIVATE ${REPO_ROOT}/lib/hal)

target_link_libraries(AsyncPageTurnTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(AsyncPageTurnTest)
