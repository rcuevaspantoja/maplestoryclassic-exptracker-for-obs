#pragma once

#include "image.hpp"

#include <array>
#include <string_view>

namespace expmeter {

// Builds a FloatMap from '#'/'+'/'.' rows, with `margin` blank cells on every side.
template<size_t N> FloatMap bitmapFromRows(const std::array<std::string_view, N> &rows, int margin = 0);

// Draws `bitmap` (native pixels) at `scale` into an out_w x out_h window. Native cell (0,0)
// starts at (origin_x, origin_y) in window pixels. Each output pixel is the average over a
// 4x4 grid of samples, which approximates how the game stretches its HUD.
FloatMap renderScaled(const FloatMap &bitmap, double scale, double origin_x, double origin_y, int out_w, int out_h);

// Samples a stretched image back onto the native pixel grid. Native pixel (u, v), counted
// from the stretched point (origin_x, origin_y), is read bilinearly at
// origin + (u + 0.5 + phase) * scale - 0.5. Samples outside the image are black.
RgbaImage resampleNative(const ImageView &image, double origin_x, double origin_y, double scale, double phase_x,
			 double phase_y, int u0, int v0, int width, int height);

// Normalized cross-correlation of `model` against the same-sized window of `image` at (x0, y0).
double ncc(const FloatMap &image, int x0, int y0, const FloatMap &model);

template<size_t N> FloatMap bitmapFromRows(const std::array<std::string_view, N> &rows, int margin)
{
	const int w = static_cast<int>(rows[0].size());
	const int h = static_cast<int>(N);
	FloatMap out(w + 2 * margin, h + 2 * margin);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x) {
			const char c = rows[y][x];
			out.at(x + margin, y + margin) = c == '#' ? 1.0f : (c == '+' ? 0.5f : 0.0f);
		}
	return out;
}

} // namespace expmeter
