#pragma once

#include "image.hpp"
#include "locator.hpp"

#include <string>

namespace expmeter {

struct OcrText {
	std::string text;
	double confidence = 0.0; // worst per-glyph match score, in [0, 1]
};

// Decodes one line of the small HUD font. `line` must be native scale and
// glyphs::kExpGlyphHeight + 2 rows tall (one blank row above and below the glyphs).
// Decoding stops after the closing ']'.
OcrText decodeExpLine(const FloatMap &line);

// Reads the EXP text right of the label at a native-scale location. Tries a one-pixel
// vertical jitter and returns the best-scoring line.
OcrText readExpText(const ImageView &image, const HudLocation &location);

// Reads the level from the orange boxes. Returns 0 when it is not visible or contains
// a digit with no known template.
int readLevel(const ImageView &image, const HudLocation &location);

} // namespace expmeter
