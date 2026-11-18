add_executable(FirmwareBoardTagTest
  FirmwareBoardTagTest.cpp
  ${REPO_ROOT}/src/network/FirmwareBoardTag.cpp
)

target_compile_definitions(FirmwareBoardTagTest PRIVATE LECTOR_BOARD_NAME="x4pro")
target_include_directories(FirmwareBoardTagTest PRIVATE ${REPO_ROOT}/src/network)

target_link_libraries(FirmwareBoardTagTest PRIVATE
  GTest::gtest_main
)

gtest_discover_tests(FirmwareBoardTagTest)
