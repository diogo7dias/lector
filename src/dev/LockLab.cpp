#ifdef LECTOR_LOCK_LAB

#include "dev/LockLab.h"

#include <GfxRenderer.h>

#include <cstring>

#include "CrossPointState.h"

namespace locklab {
namespace {

// The tone curves. A .pxc carries four levels and nothing else, so a four-entry table is
// the entire curve available: anything a converter can do to the midtones after the fact
// is some permutation of these four numbers.
constexpr uint8_t kLevelMaps[16][4] = {
    {0, 1, 2, 3},  // Identity: the image as the encoder made it
    {1, 2, 2, 3},  // Lift blacks: shadow detail out of a picture that reads as a blob
    {0, 1, 3, 3},  // Crush whites: highlights to paper white, for a washed-out scan
    {0, 0, 3, 3},  // High contrast: the midtones gone, which is what the panel does anyway
                   // when the base pass is wrong, so it doubles as a control
    {0, 0, 2, 3},  // Darken lines: level 1 to ink, midtone kept. Line art whose strokes
                   // read grey rather than black, without flattening the background.
    {0, 0, 1, 3},  // Heavy ink: as above and the midtone dropped a step. A background that
                   // is too pale to sit behind white subject matter.
    {0, 2, 3, 3},  // Bright mids: level 1 up to the midtone. Opens a picture that lost its
                   // shadow detail to a base pass darker than the encoder assumed.
    {0, 3, 3, 3},  // Ink on white: only true black survives. Maximum separation for pen
                   // and ink, and the widest black-to-white span the format can express.
    // The eight above move the picture. The eight below map the corners of what four
    // levels can express at all, so a tester can find the ceiling rather than guess at it.
    {0, 1, 2, 2},  // Dim whites: paper white down a step. Less glare under a frontlight,
                   // and it shows whether the panel's white is the thing that is too pale.
    {0, 1, 1, 3},  // Compress mids: both midtones to the darker one. A denser picture with
                   // one fewer grey.
    {0, 2, 2, 3},  // Split mids: both midtones to the lighter one. The same trade upward.
    {1, 1, 2, 3},  // Lift shadows: true black never asked for. If the ghost survives this
                   // unchanged, the ghost is not coming from the ink plane.
    {0, 0, 0, 3},  // Ink only: everything but paper white goes to ink. The most black the
                   // format can put on the glass.
    {1, 2, 3, 3},  // Airy: every level up one. The brightest reading of the same file.
    {2, 2, 3, 3},  // No black: level 0 never driven. A control for the base pass, since a
                   // ghost that persists here cannot be blamed on black.
    {1, 1, 3, 3},  // Two tone soft: two levels, neither of them extreme. High contrast
                   // without the hard edges.
};

constexpr uint16_t kRowsPerReadValues[5] = {0, 1, 4, 8, 16};

}  // namespace

PxcRenderOptions optionsFor(const LockLabState& state) {
  PxcRenderOptions o;
  o.dither = static_cast<PxcRenderOptions::Dither>(state.dither);
  o.passes = static_cast<PxcRenderOptions::Passes>(state.passes);
  memcpy(o.levelMap, kLevelMaps[state.levelMap < 16 ? state.levelMap : 0], sizeof(o.levelMap));
  o.invert = state.invert != 0;
  // Position 0 is Auto, which is the -1 the renderer reads as "ask
  // sleepGrayscaleBaseRefresh()"; the rest line up with HalDisplay::RefreshMode.
  o.grayBaseRefresh = (state.grayBaseRefresh == 0) ? -1 : static_cast<int8_t>(state.grayBaseRefresh - 1);
  o.wholeFileCache = state.wholeFileCache != 0;
  o.rowsPerRead = kRowsPerReadValues[state.rowsPerRead < 5 ? state.rowsPerRead : 0];
  return o;
}

}  // namespace locklab

#endif  // LECTOR_LOCK_LAB
