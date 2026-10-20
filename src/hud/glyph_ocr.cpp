#include "glyph_ocr.hpp"

#include "glyphs.hpp"
#include "parser.hpp"

#include <cmath>
#include <vector>

namespace expmeter {

namespace {

constexpr float kInkThreshold = 0.35f;  // a column with less ink than this is blank
constexpr double kMinGlyphScore = 0.55; // below this, a column is skipped as noise
// Level digits are crisp; an unknown digit (e.g. an 8 vs the 9 template) differs by
// only ~3 of 49 pixels, so anything short of a near-exact match is rejected.
constexpr double kLevelMinScore = 0.96;
constexpr int kLevelMinOrangeRows = 4;

struct Candidate {
	double score = -1.0;
	const glyphs::ExpGlyph *glyph = nullptr;
	int x = 0;
	int width = 0;
};

int glyphWidth(const glyphs::ExpGlyph &g)
{
	return static_cast<int>(g.rows[0].size());
}

// 1 - normalized absolute difference, with a penalty for ink in the blank columns
// that must surround every glyph.
double scoreGlyph(const FloatMap &line, const glyphs::ExpGlyph &g, int x)
{
	const int w = glyphWidth(g);
	double diff = 0.0;
	for (int r = 0; r < glyphs::kExpGlyphHeight; ++r) {
		for (int c = 0; c < w; ++c)
			diff += std::fabs(line.at(x + c, r + 1) - glyphs::cellValue(g.rows[r][c]));
		if (x > 0)
			diff += 0.5 * line.at(x - 1, r + 1);
		diff += 0.5 * line.at(x + w, r + 1);
	}
	return 1.0 - diff / (w * glyphs::kExpGlyphHeight);
}

} // namespace

OcrText decodeExpLine(const FloatMap &line)
{
	OcrText out;
	out.confidence = 1.0;
	const int n = line.width;
	int x = 0;
	while (x < n - 1) {
		float column = 0.0f;
		for (int r = 1; r <= glyphs::kExpGlyphHeight; ++r)
			column = std::max(column, line.at(x, r));
		if (column < kInkThreshold) {
			++x;
			continue;
		}

		Candidate best;
		for (const auto &g : glyphs::kExpGlyphs) {
			const int w = glyphWidth(g);
			for (int dx = -1; dx <= 0; ++dx) {
				const int xx = x + dx;
				if (xx < 0 || xx + w + 1 > n)
					continue;
				const double score = scoreGlyph(line, g, xx);
				if (score > best.score + 1e-6)
					best = {score, &g, xx, w};
			}
		}
		if (!best.glyph || best.score < kMinGlyphScore) {
			++x;
			continue;
		}
		out.text.push_back(best.glyph->ch);
		out.confidence = std::min(out.confidence, best.score);
		x = best.x + best.width + 1;
		if (best.glyph->ch == ']')
			break;
	}
	if (out.text.empty())
		out.confidence = 0.0;
	return out;
}

OcrText readExpText(const ImageView &image, const HudLocation &location)
{
	const int x0 = location.x + glyphs::kTextOffsetX;
	const int width = std::min(glyphs::kTextScanWidth, image.width - x0);
	const int height = glyphs::kExpGlyphHeight + 2;
	OcrText best;
	bool best_parses = false;
	if (x0 < 0 || width <= 0)
		return best;

	for (int dy = -1; dy <= 1; ++dy) {
		const int y0 = location.y + glyphs::kTextOffsetY - 1 + dy;
		if (y0 < 0 || y0 + height > image.height)
			continue;
		FloatMap line(width, height);
		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < width; ++x) {
				int r, g, b;
				image.rgb(x0 + x, y0 + y, r, g, b);
				line.at(x, y) = textInk(r, g, b);
			}
		}
		OcrText text = decodeExpLine(line);
		const bool parses = parseExpText(text.text).has_value();
		if ((parses && !best_parses) || (parses == best_parses && text.confidence > best.confidence)) {
			best = std::move(text);
			best_parses = parses;
		}
	}
	return best;
}

int readLevel(const ImageView &image, const HudLocation &location)
{
	const int scan_x0 = location.x + glyphs::kLevelScanOffsetX;
	const int box_y0 = location.y + glyphs::kLevelBoxOffsetY;
	if (scan_x0 < 0 || box_y0 < 0 || box_y0 + glyphs::kLevelBoxHeight > image.height)
		return 0;
	const int scan_x1 = std::min(image.width, scan_x0 + glyphs::kLevelScanWidth);

	// Find the orange boxes as runs of columns that contain enough orange pixels.
	std::vector<int> box_starts;
	int run_start = -1;
	for (int x = scan_x0; x <= scan_x1; ++x) {
		int orange = 0;
		if (x < scan_x1) {
			for (int y = box_y0; y < box_y0 + glyphs::kLevelBoxHeight; ++y) {
				int r, g, b;
				image.rgb(x, y, r, g, b);
				orange += isLevelOrange(r, g, b) ? 1 : 0;
			}
		}
		if (orange >= kLevelMinOrangeRows) {
			if (run_start < 0)
				run_start = x;
		} else if (run_start >= 0) {
			const int w = x - run_start;
			if (w < glyphs::kLevelBoxWidth - 1 || w > glyphs::kLevelBoxWidth + 1)
				return 0; // not the layout we know
			box_starts.push_back(run_start);
			run_start = -1;
		}
	}
	if (box_starts.empty() || box_starts.size() > 3)
		return 0;

	int level = 0;
	for (int bx : box_starts) {
		const int gx = bx + glyphs::kLevelGlyphInsetX;
		const int gy = box_y0 + glyphs::kLevelGlyphInsetY;
		char digit = 0;
		double best = 0.0;
		for (const auto &g : glyphs::kLevelGlyphs) {
			double diff = 0.0;
			for (int r = 0; r < glyphs::kLevelGlyphSize; ++r) {
				for (int c = 0; c < glyphs::kLevelGlyphSize; ++c) {
					int pr, pg, pb;
					image.rgb(gx + c, gy + r, pr, pg, pb);
					diff += std::fabs(whiteness(pr, pg, pb) - glyphs::cellValue(g.rows[r][c]));
				}
			}
			const double score = 1.0 - diff / (glyphs::kLevelGlyphSize * glyphs::kLevelGlyphSize);
			if (score > best) {
				best = score;
				digit = g.ch;
			}
		}
		if (best < kLevelMinScore)
			return 0;
		level = level * 10 + (digit - '0');
	}
	return level;
}

} // namespace expmeter
