add_executable(HintBandTest
  HintBandTest.cpp
  HintBandInputAuditTest.cpp
)

target_include_directories(HintBandTest PRIVATE
  ${REPO_ROOT}/src
)

target_compile_definitions(HintBandTest PRIVATE
  MAPPED_INPUT_MANAGER_SOURCE="${REPO_ROOT}/src/MappedInputManager.cpp"
)

target_link_libraries(HintBandTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(HintBandTest)
