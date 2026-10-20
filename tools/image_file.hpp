#pragma once

#include "hud/image.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace expmeter {

// RGBA image loaded from disk (PNG, BMP, ...), for the CLI and tests.
struct ImageFile {
	int width = 0;
	int height = 0;
	std::vector<uint8_t> pixels;

	ImageView view() const { return {pixels.data(), width, height, width * 4, PixelFormat::RGBA}; }
};

std::optional<ImageFile> loadImageFile(const std::string &path);

} // namespace expmeter
