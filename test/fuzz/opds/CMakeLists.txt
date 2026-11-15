# OPDS catalogue parser (lib/OpdsParser) on expat, fed by untrusted network servers.
# Same expat configuration the firmware builds with (see test/opds_parser/CMakeLists.txt).
lector_add_fuzzer(opds
  fuzz_opds.cpp
  ${REPO_ROOT}/lib/OpdsParser/OpdsParser.cpp
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
)

target_include_directories(fuzz_opds PRIVATE
  ${REPO_ROOT}/test/opds_parser/stubs
  ${REPO_ROOT}/lib/OpdsParser
  ${REPO_ROOT}/lib/XmlParserUtils
  ${REPO_ROOT}/lib/expat
)

target_compile_definitions(fuzz_opds PRIVATE XML_GE=0 XML_CONTEXT_BYTES=0)

