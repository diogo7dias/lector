#pragma once
#include <cstdint>

// Shared boot-logo table. The image byte arrays live in the generated
// bootlogoN.h headers; they are `static` (internal linkage), so including them
// in more than one translation unit bakes a duplicate copy of every image into
// flash. This module includes them ONCE (BootLogos.cpp) and exposes a single
// shared table that BootActivity and SleepActivity both reference.
//
// kAll[0 .. kSkullCount-1] are the original "READ TILL YOU DIE" skull crests
// (the branded startup logos); the rest are the extra user logos. The default
// startup logo is drawn from the skull subset; the "Random Logo" sleep mode
// draws from the whole table.
namespace bootlogos {
extern const uint8_t* const kAll[];
extern const int kCount;       // total logos in kAll
extern const int kSkullCount;  // leading branded skull crests (kAll[0..kSkullCount-1])
}  // namespace bootlogos
