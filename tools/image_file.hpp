#pragma once

#include "hud/image.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace expmeter {

// RGBA image loaded from disk (PNG, BMP, ...), for the CLI and tests.
using ImageFile = RgbaImage;

std::optional<ImageFile> loadImageFile(const std::string &path);

} // namespace expmeter
