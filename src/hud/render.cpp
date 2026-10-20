#include "render.hpp"

#include <cmath>

namespace expmeter {

FloatMap renderScaled(const FloatMap &bitmap, double scale, double origin_x, double origin_y, int out_w, int out_h)
{
	constexpr int kSamples = 4;
	FloatMap out(out_w, out_h);
	for (int y = 0; y < out_h; ++y) {
		for (int x = 0; x < out_w; ++x) {
			float sum = 0.0f;
			for (int sy = 0; sy < kSamples; ++sy) {
				const double v = std::floor((y + (sy + 0.5) / kSamples - origin_y) / scale);
				if (v < 0 || v >= bitmap.height)
					continue;
				for (int sx = 0; sx < kSamples; ++sx) {
					const double u = std::floor((x + (sx + 0.5) / kSamples - origin_x) / scale);
					if (u < 0 || u >= bitmap.width)
						continue;
					sum += bitmap.at(static_cast<int>(u), static_cast<int>(v));
				}
			}
			out.at(x, y) = sum / (kSamples * kSamples);
		}
	}
	return out;
}

RgbaImage resampleNative(const ImageView &image, double origin_x, double origin_y, double scale, double phase_x,
			 double phase_y, int u0, int v0, int width, int height)
{
	RgbaImage out;
	out.width = width;
	out.height = height;
	out.pixels.assign(static_cast<size_t>(width) * height * 4, 0);
	for (int v = 0; v < height; ++v) {
		const double sy = origin_y + (v0 + v + 0.5 + phase_y) * scale - 0.5;
		const int y = static_cast<int>(std::floor(sy));
		const double fy = sy - y;
		if (y < 0 || y + 1 >= image.height)
			continue;
		for (int u = 0; u < width; ++u) {
			const double sx = origin_x + (u0 + u + 0.5 + phase_x) * scale - 0.5;
			const int x = static_cast<int>(std::floor(sx));
			const double fx = sx - x;
			if (x < 0 || x + 1 >= image.width)
				continue;
			int c[4][3];
			image.rgb(x, y, c[0][0], c[0][1], c[0][2]);
			image.rgb(x + 1, y, c[1][0], c[1][1], c[1][2]);
			image.rgb(x, y + 1, c[2][0], c[2][1], c[2][2]);
			image.rgb(x + 1, y + 1, c[3][0], c[3][1], c[3][2]);
			uint8_t *p = &out.pixels[(static_cast<size_t>(v) * width + u) * 4];
			for (int ch = 0; ch < 3; ++ch) {
				const double top = c[0][ch] * (1 - fx) + c[1][ch] * fx;
				const double bottom = c[2][ch] * (1 - fx) + c[3][ch] * fx;
				p[ch] = static_cast<uint8_t>(std::lround(top * (1 - fy) + bottom * fy));
			}
			p[3] = 255;
		}
	}
	return out;
}

double ncc(const FloatMap &image, int x0, int y0, const FloatMap &model)
{
	if (x0 < 0 || y0 < 0 || x0 + model.width > image.width || y0 + model.height > image.height)
		return -1.0;
	const double n = static_cast<double>(model.width) * model.height;
	double sp = 0, sm = 0, spp = 0, smm = 0, spm = 0;
	for (int y = 0; y < model.height; ++y)
		for (int x = 0; x < model.width; ++x) {
			const double p = image.at(x0 + x, y0 + y);
			const double m = model.at(x, y);
			sp += p;
			sm += m;
			spp += p * p;
			smm += m * m;
			spm += p * m;
		}
	const double vp = spp - sp * sp / n;
	const double vm = smm - sm * sm / n;
	if (vp <= 1e-9 || vm <= 1e-9)
		return -1.0;
	return (spm - sp * sm / n) / std::sqrt(vp * vm);
}

} // namespace expmeter
