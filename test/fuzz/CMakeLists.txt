if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  message(FATAL_ERROR "LECTOR_FUZZ needs clang (libFuzzer): CC=clang CXX=clang++")
endif()

set(LECTOR_FUZZ_FLAGS -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -g)

# lector_add_fuzzer(<name> <sources>...): builds fuzz_<name> and a ctest that replays
# <name>/corpus once (-runs=0), so every saved input stays a regression check.
function(lector_add_fuzzer name)
  add_executable(fuzz_${name} ${ARGN})
  target_compile_options(fuzz_${name} PRIVATE ${LECTOR_FUZZ_FLAGS})
  target_link_options(fuzz_${name} PRIVATE ${LECTOR_FUZZ_FLAGS})
  target_link_libraries(fuzz_${name} PRIVATE crosspoint_test_common)
  add_test(NAME fuzz_${name}_corpus COMMAND fuzz_${name} -runs=0 ${CMAKE_CURRENT_SOURCE_DIR}/corpus)
endfunction()

# One folder per target; a folder joins the build once it has a CMakeLists.txt.
foreach(target zip inflate css opds chapter metadata image opf_text)
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${target}/CMakeLists.txt)
    add_subdirectory(${target})
  endif()
endforeach()
