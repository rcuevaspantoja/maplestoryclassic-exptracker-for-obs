#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace expmeter {

enum class PixelFormat { RGBA, BGRA };

// Non-owning view over a packed 8-bit, 4-channel image.
struct ImageView {
	const uint8_t *data = nullptr;
	int width = 0;
	int height = 0;
	int stride = 0; // bytes per row
	PixelFormat format = PixelFormat::RGBA;

	void rgb(int x, int y, int &r, int &g, int &b) const
	{
		const uint8_t *p = data + static_cast<size_t>(y) * stride + static_cast<size_t>(x) * 4;
		if (format == PixelFormat::RGBA) {
			r = p[0];
			g = p[1];
			b = p[2];
		} else {
			r = p[2];
			g = p[1];
			b = p[0];
		}
	}
};

// Single-channel float image, values usually in [0, 1].
struct FloatMap {
	int width = 0;
	int height = 0;
	std::vector<float> values;

	FloatMap() = default;
	FloatMap(int w, int h) : width(w), height(h), values(static_cast<size_t>(w) * h, 0.0f) {}

	float at(int x, int y) const { return values[static_cast<size_t>(y) * width + x]; }
	float &at(int x, int y) { return values[static_cast<size_t>(y) * width + x]; }
};

// How "white" a pixel is: 0 for anything darker than mid gray, 1 for near white.
inline float whiteness(int r, int g, int b)
{
	const int m = std::min(r, std::min(g, b));
	return std::clamp((m - 60) / 140.0f, 0.0f, 1.0f);
}

// Ink of the EXP text: white digits plus the green brackets around the percentage.
inline float textInk(int r, int g, int b)
{
	const float green = r > 80 ? std::clamp((g - b - 60) / 100.0f, 0.0f, 1.0f) : 0.0f;
	return std::max(whiteness(r, g, b), green);
}

inline bool isLevelOrange(int r, int g, int b)
{
	return r > 200 && g > 90 && g < 190 && b < 90;
}

} // namespace expmeter
