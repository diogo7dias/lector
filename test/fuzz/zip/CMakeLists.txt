lector_add_fuzzer(zip
  ${CMAKE_CURRENT_SOURCE_DIR}/fuzz_zip.cpp
  ${REPO_ROOT}/lib/ZipFile/ZipFile.cpp
  ${REPO_ROOT}/lib/miniz/src/InflateStream.cpp
  ${REPO_ROOT}/lib/miniz/src/miniz_impl.c
  ${REPO_ROOT}/lib/Memory/BuildScratch.cpp
)
target_include_directories(fuzz_zip PRIVATE
  ${REPO_ROOT}/test/zip_file/stubs
  ${REPO_ROOT}/lib/ZipFile
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/miniz/src
)
