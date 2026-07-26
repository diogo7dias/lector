#pragma once

#include <cstring>

#define FOOTNOTE_NUMBER_LEN 32
// 96 was not enough: calibre-generated EPUBs with long filenames and URL-encoded
// characters routinely exceed it (e.g. "Author-Title_split_NNN.html#_ftnN", once
// encoded, runs to about 150 chars). This struct is serialized into the page
// cache, so SECTION_FILE_VERSION must be bumped whenever this changes.
#define FOOTNOTE_HREF_LEN 256

struct FootnoteEntry {
  char number[FOOTNOTE_NUMBER_LEN];
  char href[FOOTNOTE_HREF_LEN];

  FootnoteEntry() {
    number[0] = '\0';
    href[0] = '\0';
  }
};
