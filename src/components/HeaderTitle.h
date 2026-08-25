#pragma once

#include <string>

// How a screen's title is set apart from the list under it. The title is drawn in the
// same UI font and the same size as the rows (a larger size read as a second heading
// next to the home screen's own text), and Cozette registers its regular face in both
// the regular and the bold slot, so asking for bold changes nothing on screen. Brackets
// do the job the weight would have: [Settings] over the rows it belongs to.
namespace header_title {

inline std::string decorate(const char* title) {
  if (title == nullptr || title[0] == '\0') return std::string();
  const std::string text(title);
  // A caller that brackets its own title keeps it, rather than ending up [[like this]].
  if (text.front() == '[' && text.back() == ']') return text;
  return "[" + text + "]";
}

}  // namespace header_title
