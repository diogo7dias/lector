add_executable(KOReaderDocumentIdTest
  KOReaderDocumentIdTest.cpp
  ${REPO_ROOT}/lib/KOReaderSync/KOReaderDocumentId.cpp
)

target_include_directories(KOReaderDocumentIdTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/KOReaderSync
)

target_link_libraries(KOReaderDocumentIdTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(KOReaderDocumentIdTest)
