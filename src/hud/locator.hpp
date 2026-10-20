#pragma once

#include "image.hpp"

#include <map>
#include <optional>

namespace expmeter {

// Where the "EXP." label was found: top-left corner of the label template, in image pixels.
struct HudLocation {
	int x = 0;
	int y = 0;
	double scale = 1.0;
	double score = 0.0; // normalized cross-correlation in [-1, 1]

	bool isNative() const { return scale > 0.97 && scale < 1.03; }
};

// Finds the "EXP." label by normalized cross-correlation against its template.
class Locator {
public:
	// Native-scale matches at or above this score are trusted.
	static constexpr double kNativeAcceptScore = 0.90;
	// Scaled matches are blurrier, so they are only used to diagnose "wrong scale".
	static constexpr double kScaledAcceptScore = 0.80;

	// Tries the hint first (cheap), then a full search of the bottom of the frame.
	std::optional<HudLocation> locate(const ImageView &image,
					  const std::optional<HudLocation> &hint = std::nullopt);

private:
	const FloatMap &templateFor(double scale);
	std::optional<HudLocation> search(const FloatMap &region, int offset_x, int offset_y, double scale, int x0,
					  int y0, int x1, int y1);

	std::map<int, FloatMap> templates_; // keyed by scale * 100
};

} // namespace expmeter
