find_package(Python3 COMPONENTS Interpreter)
find_program(NODE_EXECUTABLE node)

if(Python3_FOUND AND NODE_EXECUTABLE)
  add_test(NAME ArticleEpubBuilder
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_article_epub.py
            ${NODE_EXECUTABLE}
            ${REPO_ROOT}/src/network/html/FilesPage.html
            ${REPO_ROOT}/src/network/html/js/jszip.min.js)
else()
  # Without node the builder is not tested at all: say so instead of passing quietly.
  message(WARNING "ArticleEpubBuilder test skipped: needs python3 and node")
endif()
