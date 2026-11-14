// Kept by hand.
#pragma once

#define CHAREINK_14_FONT_ID (75174429)
// Active UI font ids (menus/lists/hints). Bound at boot + on language change to
// either the Cozette family (default, most languages) or the Ubuntu family
// (Arabic/Hebrew fallback). The id VALUES are derived from the Cozette headers.
#define UI_10_FONT_ID (-2061243555)
#define UI_12_FONT_ID (1074320014)
// Permanent Ubuntu ids: always bound to the Ubuntu family regardless of language.
// Used by the language-select list so Arabic/Hebrew native names never box.
#define UBUNTU_10_FONT_ID (-350896663)
#define UBUNTU_12_FONT_ID (-884914451)
#define SMALL_FONT_ID (674098198)
// Paragraph numbers: Cozette digits only, at 1x, 2x and 3x its 13px pixel grid.
#define PARA_NUM_13_FONT_ID (1418093217)
#define PARA_NUM_26_FONT_ID (-1730452893)
#define PARA_NUM_39_FONT_ID (906157361)
// Font ID 0 is reserved as the "not found" sentinel.
// Guard against any hash accidentally producing 0.
static_assert(CHAREINK_14_FONT_ID != 0, "Font ID collision with sentinel");
static_assert(UI_10_FONT_ID != 0, "Font ID collision with sentinel");
static_assert(UI_12_FONT_ID != 0, "Font ID collision with sentinel");
static_assert(UBUNTU_10_FONT_ID != 0, "Font ID collision with sentinel");
static_assert(UBUNTU_12_FONT_ID != 0, "Font ID collision with sentinel");
static_assert(SMALL_FONT_ID != 0, "Font ID collision with sentinel");
static_assert(PARA_NUM_13_FONT_ID != 0 && PARA_NUM_26_FONT_ID != 0 && PARA_NUM_39_FONT_ID != 0,
              "Font ID collision with sentinel");
