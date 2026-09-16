// Compiles the FreeInk SDK's stb_truetype-backed TtfFont into this library on
// TTF-capable builds. Only the rasterizer is wanted, not the whole FreeInkBook
// engine: linking the engine would also build its vendored miniz/expat copies,
// which collide with lib/miniz and lib/expat. Same include-the-source pattern
// FreeInkBook itself uses for its vendored code (src/vendor/*.c).
#ifdef CROSSPOINT_TTF_READER
#include "../../freeink-sdk/libs/book/FreeInkBook/src/render/TtfFont.cpp"
#endif
