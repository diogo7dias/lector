add_executable(SectionCacheValidityTest SectionCacheValidityTest.cpp)
target_include_directories(SectionCacheValidityTest PRIVATE ${REPO_ROOT}/lib/Epub/Epub)
target_link_libraries(SectionCacheValidityTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(SectionCacheValidityTest)

# Compile the actual header writer/loader and constants with an in-memory SD adapter.
# As in grid_screens, extract device-independent production bodies, never copies of logic.
set(section_source "${REPO_ROOT}/lib/Epub/Epub/Section.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${section_source}")
file(READ "${section_source}" section)
string(FIND "${section}" "namespace {" constants_start)
string(FIND "${section}" "}  // namespace" constants_end)
math(EXPR constants_length "${constants_end} - ${constants_start} + 1")
string(SUBSTRING "${section}" ${constants_start} ${constants_length} constants)
string(FIND "${section}" "void Section::writeSectionFileHeader(" methods_start)
string(FIND "${section}" "bool Section::clearCache()" methods_end)
math(EXPR methods_length "${methods_end} - ${methods_start}")
string(SUBSTRING "${section}" ${methods_start} ${methods_length} methods)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/SectionHeader.inc" "${constants}\n${methods}")
target_sources(SectionCacheValidityTest PRIVATE SectionHeaderTest.cpp)
target_include_directories(SectionCacheValidityTest PRIVATE
  stubs ${CMAKE_CURRENT_BINARY_DIR} ${REPO_ROOT}/lib/Serialization)
