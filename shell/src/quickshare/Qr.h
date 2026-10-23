#pragma once

#include <string>
#include <vector>

namespace m3::quickshare::qr {

// A QR code for `text` (byte mode, error correction M, the smallest version
// that holds it): rows of modules, true = dark, without the quiet zone.
// Empty if it doesn't fit (over ~2.3 KB).
std::vector<std::vector<bool>> encode(const std::string& text);

} // namespace m3::quickshare::qr
