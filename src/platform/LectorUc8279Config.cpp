// X4 Pro UC8279 config, selected with -DFREEINK_UC8279_X4_CONFIG=lectorUc8279X4Config
// in Speed Kit builds only. It is the SDK default, held mutable so the Speed Test sweep
// can try other frame rates (PLL) and fast-refresh temperature slots (TSSET) between
// refreshes: the driver keeps a reference and reads both on every refresh.

#include "LectorUc8279Config.h"

namespace freeink {

Uc8279X4Config& lectorUc8279X4Tune() {
  static Uc8279X4Config cfg = uc8279X4DefaultConfig();
  return cfg;
}

const Uc8279X4Config& lectorUc8279X4Config() { return lectorUc8279X4Tune(); }

}  // namespace freeink
