#pragma once

// PlatformIO builds share byte-identical generated font tables (share_builtin_tables.py).
// Host builds use the face headers as they are; those stay the converter's output.
#ifdef LECTOR_SHARED_BUILTIN_FONT_TABLES
#include <BuiltinFontsShared.generated.h>
#else
#include <builtinFonts/all_faces.h>
#endif
