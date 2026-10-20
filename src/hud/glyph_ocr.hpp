#pragma once

#include "image.hpp"
#include "locator.hpp"

#include <string>

namespace expmeter {

struct OcrText {
	std::string text;
	double confidence = 0.0; // worst per-glyph match score, in [0, 1]
	double mean_score = 0.0; // average per-glyph match score
};

// Decodes one line of the small HUD font. `line` must be native scale and
// glyphs::kExpGlyphHeight + 2 rows tall (one blank row above and below the glyphs).
// Decoding stops after the closing ']'.
OcrText decodeExpLine(const FloatMap &line);

// Reads the EXP text right of the label. `image` must hold the HUD at native scale (a
// stretched HUD is resampled first, see unstretchHud). Tries a one-pixel vertical jitter.
OcrText readExpText(const ImageView &image, const HudLocation &location);

// Reads the level from the orange boxes of a native-scale HUD. Returns 0 when it is not
// visible, contains a digit with no known template, or no template wins with at least
// `min_score` and a lead of `min_margin` over the runner-up.
// Native captures are crisp: an unknown digit (e.g. an 8 vs the 9 template) differs by only
// ~3 of 49 pixels, so the default demands a near-exact match.
int readLevel(const ImageView &image, const HudLocation &location, double min_score = 0.96, double min_margin = 0.0);

} // namespace expmeter
