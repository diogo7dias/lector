add_executable(refresh_intent_tests
  RefreshIntentTest.cpp
)

target_link_libraries(refresh_intent_tests PRIVATE crosspoint_test_common GTest::gtest_main)
target_include_directories(refresh_intent_tests PRIVATE
  ${REPO_ROOT}/lib/GfxRenderer
)

gtest_discover_tests(refresh_intent_tests)
