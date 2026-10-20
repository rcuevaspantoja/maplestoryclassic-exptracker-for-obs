#include "hud_reader.hpp"

#include "glyph_ocr.hpp"
#include "glyphs.hpp"
#include "parser.hpp"
#include "render.hpp"

namespace expmeter {

namespace {

// Native area resampled around a stretched label: from the level boxes to past the EXP text.
constexpr int kNativeLeft = glyphs::kLevelScanOffsetX - 4;
constexpr int kNativeRight = glyphs::kTextOffsetX + glyphs::kTextScanWidth + 4;
constexpr int kNativeTop = -2;
constexpr int kNativeBottom = glyphs::kLevelBoxOffsetY + glyphs::kLevelBoxHeight + 4;
// Calibration only needs the text, which is much cheaper to resample.
constexpr int kTextLeft = glyphs::kTextOffsetX - 4;

bool sameLocation(const HudLocation &a, const HudLocation &b)
{
	return a.x == b.x && a.y == b.y && a.scale == b.scale;
}

} // namespace

const char *statusName(ReadStatus status)
{
	switch (status) {
	case ReadStatus::Ok:
		return "ok";
	case ReadStatus::HudNotFound:
		return "hud-not-found";
	case ReadStatus::UnsupportedScale:
		return "unsupported-scale";
	case ReadStatus::Unreadable:
		return "unreadable";
	}
	return "unknown";
}

HudReading HudReader::read(const ImageView &image, bool allow_scale_search)
{
	HudReading reading;
	const auto location = locator_.locate(image, last_location_, allow_scale_search);
	if (!location) {
		reading.status = ReadStatus::HudNotFound;
		return reading;
	}
	if (!last_location_ || !sameLocation(*last_location_, *location))
		calibration_.reset();
	last_location_ = location;
	reading.location = location;

	// A shrunk HUD loses pixels; guessing digits from it is not worth a wrong number.
	if (location->scale < 0.97) {
		reading.status = ReadStatus::UnsupportedScale;
		return reading;
	}
	if (!location->isNative())
		return readStretched(image, *location, reading);

	const OcrText text = readExpText(image, *location);
	reading.raw_text = text.text;
	reading.confidence = text.confidence;
	const auto value = parseExpText(text.text);
	if (!value || text.confidence < kMinConfidence) {
		reading.status = ReadStatus::Unreadable;
		return reading;
	}

	reading.status = ReadStatus::Ok;
	reading.exp = value->exp;
	reading.percent_x100 = value->percent_x100;
	reading.level = readLevel(image, *location);
	return reading;
}

std::optional<HudReader::Calibration> HudReader::calibrate(const ImageView &image, const HudLocation &location) const
{
	// The text sits at a fixed native offset in this resampled strip.
	const HudLocation text_location{static_cast<double>(-kTextLeft), static_cast<double>(-kNativeTop), 1.0, 1.0};
	std::optional<Calibration> best;
	double best_mean = 0.0;
	for (int ds = -6; ds <= 6; ++ds) {
		const double scale = location.scale + ds * 0.0025;
		for (int py = -2; py <= 2; ++py) {
			for (int px = -2; px <= 2; ++px) {
				const RgbaImage strip = resampleNative(image, location.x, location.y, scale, px * 0.25,
								       py * 0.25, kTextLeft, kNativeTop,
								       kNativeRight - kTextLeft,
								       kNativeBottom - kNativeTop);
				const OcrText text = readExpText(strip.view(), text_location);
				if (!parseExpText(text.text) || text.confidence < kMinStretchedConfidence)
					continue;
				if (!best || text.mean_score > best_mean) {
					best = Calibration{scale, px * 0.25, py * 0.25};
					best_mean = text.mean_score;
				}
			}
		}
	}
	return best;
}

HudReading HudReader::readStretched(const ImageView &image, const HudLocation &location, HudReading reading)
{
	const HudLocation native{static_cast<double>(-kNativeLeft), static_cast<double>(-kNativeTop), 1.0, 1.0};
	// Calibrate on the first frame at this location, and again if an older calibration stops
	// working (e.g. it was made on a frame where the text was partly covered).
	bool fresh = false;
	for (int attempt = 0; attempt < 2; ++attempt) {
		if (!calibration_ || attempt > 0) {
			if (fresh)
				break;
			calibration_ = calibrate(image, location);
			fresh = true;
			if (!calibration_)
				break;
		}
		const RgbaImage hud = resampleNative(image, location.x, location.y, calibration_->scale,
						     calibration_->phase_x, calibration_->phase_y, kNativeLeft,
						     kNativeTop, kNativeRight - kNativeLeft,
						     kNativeBottom - kNativeTop);
		const OcrText text = readExpText(hud.view(), native);
		reading.raw_text = text.text;
		reading.confidence = text.confidence;
		const auto value = parseExpText(text.text);
		if (!value || text.confidence < kMinStretchedConfidence)
			continue;

		reading.status = ReadStatus::Ok;
		reading.exp = value->exp;
		reading.percent_x100 = value->percent_x100;
		reading.level = readLevel(hud.view(), native, kStretchedLevelMinScore, kStretchedLevelMinMargin);
		return reading;
	}
	reading.status = ReadStatus::Unreadable;
	return reading;
}

} // namespace expmeter
