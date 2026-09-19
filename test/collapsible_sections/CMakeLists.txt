add_executable(CollapsibleSectionsTest
  CollapsibleSectionsTest.cpp
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp
)
target_include_directories(CollapsibleSectionsTest PRIVATE
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/include
)
target_compile_options(CollapsibleSectionsTest PRIVATE -fno-exceptions -fno-rtti)
target_link_libraries(CollapsibleSectionsTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(CollapsibleSectionsTest)
