#include "hud_reader.hpp"

#include "glyph_ocr.hpp"
#include "parser.hpp"

namespace expmeter {

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

HudReading HudReader::read(const ImageView &image)
{
	HudReading reading;
	const auto location = locator_.locate(image, last_location_);
	if (!location) {
		last_location_.reset();
		reading.status = ReadStatus::HudNotFound;
		return reading;
	}
	last_location_ = location;
	reading.location = location;

	if (!location->isNative()) {
		reading.status = ReadStatus::UnsupportedScale;
		return reading;
	}

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

} // namespace expmeter
