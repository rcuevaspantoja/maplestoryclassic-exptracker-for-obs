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
	UnsupportedScale, // label found, but the HUD is shrunk below native size
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

// Reads EXP, percentage and level from one frame. Keeps the last location (even while the
// HUD is hidden) so that later frames only re-check that spot and scale.
//
// A stretched HUD (e.g. the game at 1920x1080 draws its 1366x768 canvas 1.406x larger) is
// resampled back to native pixels and read with the exact native decoder. The stretch is
// calibrated once per location, to 0.0025 scale and a quarter pixel, by picking the
// resampling under which the EXP text decodes best.
class HudReader {
public:
	static constexpr double kMinConfidence = 0.80;          // native: exact pixel matches
	static constexpr double kMinStretchedConfidence = 0.70; // resampled: slightly softer
	static constexpr double kStretchedLevelMinScore = 0.92;
	static constexpr double kStretchedLevelMinMargin = 0.05;

	// `allow_scale_search` permits the expensive search over all scales when the HUD is not
	// where (or at the scale) it was last seen.
	HudReading read(const ImageView &image, bool allow_scale_search = true);
	void reset()
	{
		last_location_.reset();
		calibration_.reset();
	}

private:
	struct Calibration {
		double scale = 1.0;
		double phase_x = 0.0;
		double phase_y = 0.0;
	};

	HudReading readStretched(const ImageView &image, const HudLocation &location, HudReading reading);
	std::optional<Calibration> calibrate(const ImageView &image, const HudLocation &location) const;

	Locator locator_;
	std::optional<HudLocation> last_location_;
	std::optional<Calibration> calibration_; // for last_location_
};

} // namespace expmeter
