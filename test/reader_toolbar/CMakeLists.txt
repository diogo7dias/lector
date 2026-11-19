add_executable(ReaderToolbarTest ReaderToolbarTest.cpp)
target_include_directories(ReaderToolbarTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(ReaderToolbarTest PRIVATE GTest::gtest_main)
gtest_discover_tests(ReaderToolbarTest)
