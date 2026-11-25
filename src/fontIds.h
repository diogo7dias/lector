// Kept by hand.
#pragma once

#define CHAREINK_12_FONT_ID (-2100710292)
#define CHAREINK_14_FONT_ID (75174429)
#define CHAREINK_16_FONT_ID (1486844727)
// UI font ids (menus/lists/hints), bound to the Cozette family at boot. The id VALUES
// are derived from the Cozette headers.
#define UI_10_FONT_ID (-2061243555)
#define UI_12_FONT_ID (1074320014)
#define SMALL_FONT_ID (674098198)
// Paragraph numbers: Cozette digits only, at 1x, 2x and 3x its 13px pixel grid.
#define PARA_NUM_13_FONT_ID (1418093217)
#define PARA_NUM_26_FONT_ID (-1730452893)
#define PARA_NUM_39_FONT_ID (906157361)
// Font ID 0 is reserved as the "not found" sentinel.
// Guard against any hash accidentally producing 0.
static_assert(CHAREINK_12_FONT_ID != 0 && CHAREINK_14_FONT_ID != 0 && CHAREINK_16_FONT_ID != 0,
              "Font ID collision with sentinel");
static_assert(UI_10_FONT_ID != 0, "Font ID collision with sentinel");
static_assert(UI_12_FONT_ID != 0, "Font ID collision with sentinel");
static_assert(SMALL_FONT_ID != 0, "Font ID collision with sentinel");
static_assert(PARA_NUM_13_FONT_ID != 0 && PARA_NUM_26_FONT_ID != 0 && PARA_NUM_39_FONT_ID != 0,
              "Font ID collision with sentinel");
