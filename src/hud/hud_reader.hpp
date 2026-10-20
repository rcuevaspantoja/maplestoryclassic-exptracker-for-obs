#pragma once

#include "image.hpp"
#include "locator.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace expmeter {

enum class ReadStatus {
	Ok,
	HudNotFound,      // no "EXP." label in the frame
	UnsupportedScale, // label found, but the HUD is not at native size
	Unreadable,       // label found, text did not decode into a valid value
};

const char *statusName(ReadStatus status);

struct HudReading {
	ReadStatus status = ReadStatus::HudNotFound;
	int64_t exp = 0;
	int percent_x100 = 0;
	int level = 0; // 0 when unknown
	double confidence = 0.0;
	std::string raw_text;
	std::optional<HudLocation> location;

	double percent() const { return percent_x100 / 100.0; }
};

// Reads EXP, percentage and level from one frame. Keeps the last location so that
// subsequent frames only re-check that spot.
class HudReader {
public:
	static constexpr double kMinConfidence = 0.80;

	HudReading read(const ImageView &image);
	void reset() { last_location_.reset(); }

private:
	Locator locator_;
	std::optional<HudLocation> last_location_;
};

} // namespace expmeter
