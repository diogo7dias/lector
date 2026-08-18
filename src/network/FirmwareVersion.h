#pragma once

#include <cstdio>
#include <cstring>

// Version strings this firmware has to compare, and where they come from:
//
//   "lector 0.24.1"   CROSSPOINT_VERSION, built from platformio.ini's crosspoint.version
//   "lector-0.24.1"   the git tag, which GitHub reports as the release's tag_name
//   "lector 0.9.0-rc+ab12cd"  a release-candidate build
//   "lector.exp.7"    an experimental build, tagged and released as a prerelease
//   "v1.5.0"          upstream CrossPoint's own tags
//
// The old comparison ran sscanf("%d.%d.%d") straight over these. Every one of the forms
// above starts with a letter, so sscanf matched ZERO fields and left all six integers
// uninitialized — the "is the release newer" answer was whatever happened to be on the
// stack. This parses the first three-number run instead, whatever precedes it.
namespace firmware_version {

struct Semver {
  int major = 0;
  int minor = 0;
  int patch = 0;
};

// Parses the first "N.N.N" run in the string. Returns false when there is none, which is
// the honest answer for a tag like "lector.exp.7" that carries no three-part number: the
// caller must not treat an unparsable version as older or newer, only as unknown.
inline bool parse(const char* text, Semver* out) {
  if (text == nullptr || out == nullptr) return false;
  for (const char* p = text; *p != '\0'; p++) {
    if (*p < '0' || *p > '9') continue;
    // Only accept a run that starts here: stepping into the middle of "0.24.1" at the
    // "24" would read 24.1 and a missing third field.
    if (p != text) {
      const char prev = *(p - 1);
      if (prev >= '0' && prev <= '9') continue;
      if (prev == '.') continue;
    }
    Semver v;
    if (sscanf(p, "%d.%d.%d", &v.major, &v.minor, &v.patch) == 3) {
      *out = v;
      return true;
    }
  }
  return false;
}

// True when `latest` names a strictly higher version than `current`. Unknown on either
// side is false: an update the device cannot even name is not one it should install.
// A current build marked "-rc" is treated as older than the same numbers released, which
// is how a release candidate is meant to be superseded by its stable build.
inline bool isNewer(const char* latest, const char* current) {
  Semver l, c;
  if (!parse(latest, &l) || !parse(current, &c)) return false;
  if (l.major != c.major) return l.major > c.major;
  if (l.minor != c.minor) return l.minor > c.minor;
  if (l.patch != c.patch) return l.patch > c.patch;
  return current != nullptr && strstr(current, "-rc") != nullptr;
}

}  // namespace firmware_version
