#include "locator.hpp"

#include "glyphs.hpp"
#include "render.hpp"

#include <cmath>
#include <vector>

namespace expmeter {

namespace {

FloatMap whitenessMap(const ImageView &image, int x0, int y0, int x1, int y1)
{
	FloatMap map(x1 - x0, y1 - y0);
	for (int y = y0; y < y1; ++y) {
		for (int x = x0; x < x1; ++x) {
			int r, g, b;
			image.rgb(x, y, r, g, b);
			map.at(x - x0, y - y0) = whiteness(r, g, b);
		}
	}
	return map;
}

const FloatMap &labelBitmap()
{
	static const FloatMap bitmap = bitmapFromRows(glyphs::kLabel);
	return bitmap;
}

} // namespace

const FloatMap &Locator::templateFor(double scale)
{
	const int key = static_cast<int>(std::lround(scale * 100));
	auto it = templates_.find(key);
	if (it == templates_.end()) {
		const double s = key / 100.0;
		const int w = std::max(3, static_cast<int>(std::lround(glyphs::kLabelWidth * s)));
		const int h = std::max(3, static_cast<int>(std::lround(glyphs::kLabelHeight * s)));
		it = templates_.emplace(key, renderScaled(labelBitmap(), s, 0.0, 0.0, w, h)).first;
	}
	return it->second;
}

std::optional<HudLocation> Locator::search(const FloatMap &region, int offset_x, int offset_y, double scale, int x0,
					   int y0, int x1, int y1)
{
	const FloatMap &tpl = templateFor(scale);
	const int tw = tpl.width;
	const int th = tpl.height;
	x0 = std::max(x0, 0);
	y0 = std::max(y0, 0);
	x1 = std::min(x1, region.width - tw + 1);
	y1 = std::min(y1, region.height - th + 1);
	if (x1 <= x0 || y1 <= y0)
		return std::nullopt;

	const double n = static_cast<double>(tw) * th;
	double t_mean = 0.0;
	for (float v : tpl.values)
		t_mean += v;
	t_mean /= n;
	std::vector<double> tz(tpl.values.size());
	double tz_sq = 0.0;
	for (size_t i = 0; i < tz.size(); ++i) {
		tz[i] = tpl.values[i] - t_mean;
		tz_sq += tz[i] * tz[i];
	}

	// Integral images of the region (one extra row/column of zeros).
	const int iw = region.width + 1;
	std::vector<double> sum(static_cast<size_t>(iw) * (region.height + 1), 0.0);
	std::vector<double> sum_sq(sum.size(), 0.0);
	for (int y = 0; y < region.height; ++y) {
		double row = 0.0, row_sq = 0.0;
		for (int x = 0; x < region.width; ++x) {
			const double v = region.at(x, y);
			row += v;
			row_sq += v * v;
			sum[(y + 1) * iw + x + 1] = sum[y * iw + x + 1] + row;
			sum_sq[(y + 1) * iw + x + 1] = sum_sq[y * iw + x + 1] + row_sq;
		}
	}
	auto box = [iw](const std::vector<double> &s, int x, int y, int w, int h) {
		return s[(y + h) * iw + x + w] - s[y * iw + x + w] - s[(y + h) * iw + x] + s[y * iw + x];
	};

	HudLocation best;
	best.score = -1.0;
	for (int y = y0; y < y1; ++y) {
		for (int x = x0; x < x1; ++x) {
			const double s = box(sum, x, y, tw, th);
			const double mean = s / n;
			if (mean < 0.6 * t_mean || mean > 1.6 * t_mean)
				continue;
			const double var = box(sum_sq, x, y, tw, th) - s * mean;
			if (var < 1e-3 * n)
				continue;
			double num = 0.0;
			for (int ty = 0; ty < th; ++ty) {
				const float *row = &region.values[static_cast<size_t>(y + ty) * region.width + x];
				const double *trow = &tz[static_cast<size_t>(ty) * tw];
				for (int tx = 0; tx < tw; ++tx)
					num += trow[tx] * row[tx];
			}
			const double ncc = num / std::sqrt(var * tz_sq);
			if (ncc > best.score) {
				best.score = ncc;
				best.x = x + offset_x;
				best.y = y + offset_y;
				best.scale = scale;
			}
		}
	}
	if (best.score < -0.5)
		return std::nullopt;
	return best;
}

HudLocation Locator::refine(const FloatMap &band, int band_top, const HudLocation &coarse)
{
	// Text is read ~140 native pixels right of the label, so the scale has to be good to
	// about 1%. Try nearby scales and quarter-pixel offsets against the rendered label.
	constexpr int kMargin = 2;
	const int px = static_cast<int>(coarse.x) - kMargin;
	const int py = static_cast<int>(coarse.y) - band_top - kMargin;
	HudLocation best = coarse;
	for (int step = -7; step <= 7; ++step) {
		const double scale = coarse.scale + step * 0.01;
		const int w = static_cast<int>(glyphs::kLabelWidth * scale) + 2 * kMargin;
		const int h = static_cast<int>(glyphs::kLabelHeight * scale) + 2 * kMargin;
		for (int oy = -4; oy <= 4; ++oy) {
			for (int ox = -4; ox <= 4; ++ox) {
				const FloatMap model = renderScaled(labelBitmap(), scale, kMargin + ox * 0.25,
								    kMargin + oy * 0.25, w, h);
				const double score = ncc(band, px, py, model);
				if (score > best.score) {
					best.score = score;
					best.scale = scale;
					best.x = px + kMargin + ox * 0.25;
					best.y = py + band_top + kMargin + oy * 0.25;
				}
			}
		}
	}
	return best;
}

std::optional<HudLocation> Locator::locate(const ImageView &image, const std::optional<HudLocation> &hint,
					   bool allow_scale_search)
{
	if (!image.data || image.width <= 0 || image.height <= 0)
		return std::nullopt;

	if (hint) {
		// The HUD doesn't move, so re-checking the previous spot is the common, cheap path.
		const int slack = 2;
		const int hx = static_cast<int>(hint->x);
		const int hy = static_cast<int>(hint->y);
		const int margin = static_cast<int>(std::ceil(glyphs::kLabelWidth * hint->scale)) + slack + 1;
		const int x0 = std::max(0, hx - slack);
		const int y0 = std::max(0, hy - slack);
		const int x1 = std::min(image.width, hx + margin);
		const int y1 = std::min(image.height, hy + margin);
		if (x1 > x0 && y1 > y0) {
			const FloatMap region = whitenessMap(image, x0, y0, x1, y1);
			auto found = search(region, x0, y0, hint->scale, 0, 0, region.width, region.height);
			const double accept = hint->isNative() ? kNativeAcceptScore : kScaledAcceptScore;
			if (found && found->score >= accept) {
				if (hint->isNative())
					return found;
				HudLocation same = *hint; // keep the refined sub-pixel position
				same.score = found->score;
				return same;
			}
		}
	}

	// The HUD lives at the bottom of the game window. Small images are treated as crops.
	const int band_top = image.height > 200 ? image.height / 2 : 0;
	const FloatMap band = whitenessMap(image, 0, band_top, image.width, image.height);

	auto native = search(band, 0, band_top, 1.0, 0, 0, band.width, band.height);
	if (native && native->score >= kNativeAcceptScore)
		return native;

	// Next, the scale we expect: the hint's, or the supported 1920x1080 stretch.
	std::optional<HudLocation> best;
	const double expected_scale = hint && !hint->isNative() ? hint->scale : kFullHdScale;
	best = search(band, 0, band_top, expected_scale, 0, 0, band.width, band.height);
	if (best && best->score < kScaledAcceptScore)
		best.reset();
	if (!best && allow_scale_search) {
		for (int pct = 50; pct <= 200; pct += 5) {
			if (pct == 100)
				continue;
			auto found = search(band, 0, band_top, pct / 100.0, 0, 0, band.width, band.height);
			if (found && (!best || found->score > best->score))
				best = found;
		}
	}
	if (!best || best->score < kScaledAcceptScore)
		return std::nullopt;
	return refine(band, band_top, *best);
}

} // namespace expmeter
