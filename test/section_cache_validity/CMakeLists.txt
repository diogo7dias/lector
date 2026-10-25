add_executable(SectionCacheValidityTest SectionCacheValidityTest.cpp)
target_include_directories(SectionCacheValidityTest PRIVATE ${REPO_ROOT}/lib/Epub/Epub)
target_link_libraries(SectionCacheValidityTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(SectionCacheValidityTest)

# Compile the actual header writer/loader with an in-memory SD adapter; the constants
# come straight from SectionFormat.h.
# As in grid_screens, extract device-independent production bodies, never copies of logic.
set(section_source "${REPO_ROOT}/lib/Epub/Epub/Section.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${section_source}")
file(READ "${section_source}" section)
string(FIND "${section}" "void Section::writeSectionFileHeader(" methods_start)
string(FIND "${section}" "bool Section::clearCache()" methods_end)
math(EXPR methods_length "${methods_end} - ${methods_start}")
string(SUBSTRING "${section}" ${methods_start} ${methods_length} methods)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/SectionHeader.inc" "${methods}")
target_sources(SectionCacheValidityTest PRIVATE SectionHeaderTest.cpp)
target_include_directories(SectionCacheValidityTest PRIVATE
  stubs ${CMAKE_CURRENT_BINARY_DIR} ${REPO_ROOT}/lib/Serialization)
