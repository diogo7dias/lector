#pragma once
#include <cstddef>

// Real EPUBs routinely carry HTML void elements written unclosed -- <meta charset="utf-8">,
// <br>, <img ...>, <link ...> -- inside files served as XHTML. That is valid HTML and invalid
// XML, and expat rejects it the moment the parent element closes ("mismatched tag"), which
// costs the whole chapter and, to the reader, the whole book. Calibre-produced files hit this
// on every chapter via a single unclosed <meta> in <head>.
//
// The fix is a byte filter in front of expat: rewrite `<br>` to `<br/>` for the fixed set of
// void elements and leave every other byte alone. Applied to the read stream rather than to
// the unzipped HTML on disk, so books already cached are repaired without invalidating
// anything.
namespace VoidTagFixer {

// Longest tag text held back between chunks. A start tag longer than this is passed through
// untouched rather than buffered without bound; such a tag is not a void element in practice.
inline constexpr size_t MAX_CARRY = 256;

// Rewrites unclosed void elements in `buf` in place, growing it by at most one byte per void
// element. Comments, CDATA sections, processing instructions and quoted attribute values are
// passed through untouched.
//
// A tag left incomplete at the end of the chunk cannot be judged yet, so its bytes are moved
// into `carry` (length `carryLen`) and must be prepended to the next chunk by the caller. At
// end of input, pass atEof = true and nothing is held back.
//
//   buf/len : the chunk, already including any bytes carried over from last time
//   cap     : bytes writable at buf; must exceed len to leave room for insertions
//   returns : the new length, always <= cap
size_t process(char* buf, size_t len, size_t cap, char* carry, size_t& carryLen, bool atEof);

// True for the HTML elements that never have an end tag. Exposed for testing.
bool isVoidElement(const char* name, size_t len);

}  // namespace VoidTagFixer
