#pragma once

#include <string>

namespace m3::ui {

// What an icon-only button showing this Material Symbol does, in plain
// words ("arrow_back" → "Back"); unknown ones read as their name. Empty for
// an SVG path
std::string iconTip(const std::string& icon);

} // namespace m3::ui
