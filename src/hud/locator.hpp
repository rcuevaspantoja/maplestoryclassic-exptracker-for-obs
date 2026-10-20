#pragma once

#include "image.hpp"

#include <map>
#include <optional>

namespace expmeter {

// Where the "EXP." label was found: top-left corner of the label template, in image pixels.
// Scaled locations are refined to a quarter pixel and a 0.01 scale step.
struct HudLocation {
	double x = 0.0;
	double y = 0.0;
	double scale = 1.0;
	double score = 0.0; // normalized cross-correlation in [-1, 1]

	bool isNative() const { return scale > 0.97 && scale < 1.03; }
};

// Finds the "EXP." label by normalized cross-correlation against its template.
class Locator {
public:
	// Native-scale matches at or above this score are trusted.
	static constexpr double kNativeAcceptScore = 0.90;
	// Scaled matches are smoother, so they score a bit lower.
	static constexpr double kScaledAcceptScore = 0.80;
	// At 1920x1080 the game stretches its 1366x768 canvas by 1080 / 768.
	static constexpr double kFullHdScale = 1.41;

	// Tries, in order of cost: the hint's position, a native search, a search at the hint's
	// scale (or the 1920x1080 one), and (only if allowed) all scales from 0.5x to 2x.
	std::optional<HudLocation> locate(const ImageView &image, const std::optional<HudLocation> &hint = std::nullopt,
					  bool allow_scale_search = true);

private:
	const FloatMap &templateFor(double scale);
	std::optional<HudLocation> search(const FloatMap &region, int offset_x, int offset_y, double scale, int x0,
					  int y0, int x1, int y1);
	HudLocation refine(const FloatMap &band, int band_top, const HudLocation &coarse);

	std::map<int, FloatMap> templates_; // keyed by scale * 100
};

} // namespace expmeter
