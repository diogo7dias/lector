#!/usr/bin/env python3
"""Run the actual retirement/legacy migration code on the host, without Arduino.

Only storage and unrelated settings are stubbed. Extracting the production blocks
keeps this check sensitive to migration ordering and the real button-field table.
Run: python3 test/bound_action_scope/check_retired_stats.py
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

repo = Path(__file__).resolve().parents[2]
header = (repo / 'src/CrossPointSettings.h').read_text()
source = (repo / 'src/CrossPointSettings.cpp').read_text()
settings = (repo / 'src/SettingsList.h').read_text()

def between(text, start, end):
    return text[text.index(start):text.index(end, text.index(start))]

sleep_enum = between(header, '  enum SLEEP_SCREEN_MODE', '  enum SLEEP_SCREEN_COVER_MODE')
home_enum = between(header, '  enum HOME_BACK_ACTION', '  enum AUTHOR_DISPLAY')
button_enums = between(header, '  enum BOUND_BUTTON', '  // Actions that may be ticked')
bindings = between(header, '  uint8_t* buttonBinding(', '  // Which actions the Menu Pop-up')
fields = sorted(set(re.findall(r'&CrossPointSettings::(btn\w+)', bindings)))
# Keep the whole contiguous range so moving retirement ahead of legacy migration fails.
migration = between(source, '  // The power button\'s two old settings', '  // Reader font size')
assert 'retireStatsBinding' in migration
assert '.withHiddenEnumValues({CrossPointSettings::HOME_BACK_STATS})' in settings
assert 'CrossPointSettings::LP_MENU_READING_STATS' in between(settings, 'inline std::vector<uint8_t> retiredBoundFunctions()', 'inline std::vector<StrId> boundFunctionLabels()')
assert 'StrId::STR_NONE_OPT, StrId::STR_RESUME, StrId::STR_NONE_OPT, StrId::STR_SORTES' in settings

reader = (repo / 'src/activities/reader/EpubReaderActivity.cpp').read_text()
counter = next(line for line in reader.splitlines() if '++sessionPages' in line)

program = '''
#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <map>
#include <string>
#include "util/BoundActionScope.h"
#include "sleep/WakeFacePolicy.h"
using namespace bound_action;
struct Value {
  int value = -1;
  template<class T> bool is() const { return value >= 0 && value <= 255; }
  uint8_t operator|(uint8_t fallback) const { return is<uint8_t>() ? value : fallback; }
};
struct Document {
  std::map<std::string, Value> values;
  Value operator[](const char* key) const {
    auto it = values.find(key);
    return it == values.end() ? Value{} : it->second;
  }
};
struct CrossPointSettings {
''' + sleep_enum + home_enum + button_enums + '''
  uint8_t homeBackAction = HOME_BACK_RESUME;
  uint8_t longPressMenuFunction = LP_MENU_DISABLED, menuHoldFunction = LP_MENU_DISABLED;
''' + ''.join(f'  uint8_t {field} = LP_MENU_DISABLED;\n' for field in fields) + bindings + '''
  bool migrate(const Document& doc) {
    bool needsResave = false;
''' + migration + '''
    return needsResave;
  }
};
int main() {
  int sessionPages = 0;
  auto turn = [&](bool isForwardTurn, bool sortesMode = false) {
''' + counter + '''
  };
  turn(true); turn(false); turn(true, true);
  assert(sessionPages == 1);
  sessionPages = std::numeric_limits<int>::max();
  turn(true);
  assert(sessionPages == std::numeric_limits<int>::max());
  static_assert(CrossPointSettings::STATS_DASHBOARD == 7);
  static_assert(CrossPointSettings::HOME_BACK_STATS == 2);
  static_assert(CrossPointSettings::HOME_BACK_SORTES == 3);
  static_assert(LP_MENU_READING_STATS == 31);
  assert(wake_face::migrateSleepScreen(7) == 2);
  Document modern;
  modern.values["btnUiPowerSingle"] = {LP_MENU_DISABLED};
  for (uint8_t value = 0; value < LONG_PRESS_MENU_FUNCTION_COUNT; ++value) {
    CrossPointSettings s;
    s.longPressMenuFunction = s.menuHoldFunction = value;
    for (bool book : {false, true})
      for (uint8_t button = 0; button < CrossPointSettings::BOUND_BTN_COUNT; ++button)
        for (uint8_t gesture = 0; gesture < CrossPointSettings::BOUND_GESTURE_COUNT; ++gesture)
          *s.buttonBinding(book, button, gesture) = value;
    const auto expected = value == LP_MENU_READING_STATS ? LP_MENU_DISABLED : value;
    assert(s.migrate(modern) == (value == LP_MENU_READING_STATS));
    assert(s.longPressMenuFunction == expected && s.menuHoldFunction == expected);
    for (bool book : {false, true})
      for (uint8_t button = 0; button < CrossPointSettings::BOUND_BTN_COUNT; ++button)
        for (uint8_t gesture = 0; gesture < CrossPointSettings::BOUND_GESTURE_COUNT; ++gesture)
          assert(*s.buttonBinding(book, button, gesture) == expected);
    assert(!s.migrate(modern));
  }
  for (uint8_t home = 0; home < CrossPointSettings::HOME_BACK_ACTION_COUNT; ++home) {
    CrossPointSettings s;
    s.homeBackAction = home;
    assert(s.migrate(modern) == (home == CrossPointSettings::HOME_BACK_STATS));
    assert(s.homeBackAction == (home == CrossPointSettings::HOME_BACK_STATS ? CrossPointSettings::HOME_BACK_RESUME : home));
  }
  CrossPointSettings legacy;
  Document old;
  old.values["doubleClickPowerFunction"] = {LP_MENU_READING_STATS};
  assert(legacy.migrate(old));
  assert(legacy.btnBookPowerDouble == LP_MENU_DISABLED);
  assert(legacy.btnUiPowerDouble == LP_MENU_DISABLED);
}
'''
with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / 'check.cpp'
    binary = Path(directory) / 'check'
    cpp.write_text(program)
    subprocess.run([sys.argv[1] if len(sys.argv) > 1 else 'c++', '-std=c++20', '-Wall', '-Werror', '-I', str(repo / 'src'), str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('Retired stats settings: stable IDs, hidden slots, all bindings, legacy order and repeat migration passed.')
