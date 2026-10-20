#pragma once

#include "hud/hud_reader.hpp"
#include "stats/session.hpp"

#include <cstdint>
#include <mutex>
#include <string>

namespace expmeter {

// What the dock (and later the overlay) shows.
struct MeterSnapshot {
	SessionStats stats;
	bool has_frame = false;        // a filter has read at least one frame
	int64_t last_frame_age_ms = 0; // since the last frame was read
	ReadStatus status = ReadStatus::HudNotFound;
	double scale = 0.0;
	std::string source_name;
};

// The one measurement the plugin keeps. Filters feed it from their worker threads; the dock
// reads it and sends Pause/Resume/Reset from the UI thread.
class Meter {
public:
	static Meter &instance();
	static int64_t nowMs();

	// Called by a filter for every frame it reads.
	ReadingEvent onFrame(const HudReading &reading, const std::string &source_name);

	void pause();
	void resume();
	void reset();

	MeterSnapshot snapshot();

private:
	void logEvent(ReadingEvent event, const HudReading &reading, const std::string &source_name);
	void logSummary(const SessionStats &s, const std::string &source_name);

	std::mutex mutex_;
	Session session_;
	bool has_frame_ = false;
	int64_t last_frame_ms_ = 0;
	ReadStatus status_ = ReadStatus::HudNotFound;
	double scale_ = 0.0;
	std::string source_name_;
	int64_t last_summary_ms_ = 0;
};

} // namespace expmeter
