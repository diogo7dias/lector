# Same header-only JSON dependency/version as the firmware, usable without Arduino.
FetchContent_Declare(lut_lab_json
  GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
  GIT_TAG v7.4.2
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(lut_lab_json)
add_executable(LutLabTest LutLabTest.cpp ${REPO_ROOT}/src/util/FavoriteImageNames.cpp)
target_include_directories(LutLabTest PRIVATE ${REPO_ROOT}/src)
target_compile_definitions(LutLabTest PRIVATE LUT_SOURCE="${REPO_ROOT}/freeink-sdk/libs/display/FreeInkDisplay/src/lut/Ssd1677Luts.h")
target_link_libraries(LutLabTest PRIVATE crosspoint_test_common GTest::gtest_main ArduinoJson)
gtest_discover_tests(LutLabTest)
