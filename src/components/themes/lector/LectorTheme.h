#pragma once

#include "components/themes/BaseTheme.h"

// The Lector house theme. Seeded from CLASSIC (plain BaseTheme) — it inherits every
// BaseTheme drawing method and uses BaseMetrics, so it currently looks identical to
// Classic. It exists as a distinct, default-selected theme so the Lector-specific
// geometry (Phase 3) has a home without disturbing the Classic look.
class LectorTheme : public BaseTheme {};
