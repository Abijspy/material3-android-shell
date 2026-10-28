#pragma once

#include <string>

namespace m3 {

// Whether version `a` comes after `b`: "6.0.1" > "6.0", and pre-releases
// before their release, in order: "6.0-beta-1" < "6.0-beta-2" < "6.0-rc-1"
// < "6.0" (pacman's spelling, "6.0beta1", reads the same)
bool newerVersion(const std::string& a, const std::string& b);

} // namespace m3
