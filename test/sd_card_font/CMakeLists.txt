add_executable(SdCardFontTest
  SdCardFontTest.cpp
  ${REPO_ROOT}/lib/EpdFont/SdCardFont.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(SdCardFontTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs  # no-op Logging.h + stdio HalStorage.h (real ones need Arduino/SdFat)
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Utf8
)

target_link_libraries(SdCardFontTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(SdCardFontTest)
