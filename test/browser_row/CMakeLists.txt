add_executable(BrowserRowFileTest
  BrowserRowFileTest.cpp
  ${REPO_ROOT}/src/util/FavoriteImageNames.cpp
)

target_include_directories(BrowserRowFileTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(BrowserRowFileTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BrowserRowFileTest)
