#pragma once

// A small, dependency-free SHA-256 helper shared by voice packages and the
// benchmark harness. Output is always 64 lowercase hexadecimal digits.

#include <string>
#include <string_view>

namespace formant {

std::string sha256Hex(std::string_view data);

} // namespace formant
