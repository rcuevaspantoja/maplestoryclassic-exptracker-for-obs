#include "image_file.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#include <stb_image.h>

namespace expmeter {

std::optional<ImageFile> loadImageFile(const std::string &path)
{
	int w = 0, h = 0, channels = 0;
	stbi_uc *data = stbi_load(path.c_str(), &w, &h, &channels, 4);
	if (!data)
		return std::nullopt;
	ImageFile image;
	image.width = w;
	image.height = h;
	image.pixels.assign(data, data + static_cast<size_t>(w) * h * 4);
	stbi_image_free(data);
	return image;
}

} // namespace expmeter
