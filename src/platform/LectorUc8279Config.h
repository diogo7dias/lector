#pragma once

#include "driver/Uc8279X4Driver.h"

namespace freeink {
// The live X4 Pro UC8279 config, writable. Only takes effect where the build injects it
// (FREEINK_UC8279_X4_CONFIG); change it only between refreshes.
Uc8279X4Config& lectorUc8279X4Tune();
const Uc8279X4Config& lectorUc8279X4Config();
}  // namespace freeink
