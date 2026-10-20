#include "meter.hpp"

#include "format.hpp"

#include <obs-module.h>
#include <plugin-support.h>
#include <util/platform.h>

namespace expmeter {

namespace {

constexpr int64_t kSummaryIntervalMs = 30'000;

} // namespace

Meter &Meter::instance()
{
	static Meter meter;
	return meter;
}

int64_t Meter::nowMs()
{
	return static_cast<int64_t>(os_gettime_ns() / 1'000'000);
}

ReadingEvent Meter::onFrame(const HudReading &reading, const std::string &source_name)
{
	std::lock_guard<std::mutex> lock(mutex_);
	const int64_t now = nowMs();
	has_frame_ = true;
	last_frame_ms_ = now;
	status_ = reading.status;
	scale_ = reading.location ? reading.location->scale : 0.0;
	source_name_ = source_name;

	ReadingEvent event = ReadingEvent::NoChange;
	if (reading.status == ReadStatus::Ok)
		event = session_.onReading(now, reading.exp, reading.percent_x100, reading.level);
	else
		session_.onNoReading(now);

	logEvent(event, reading, source_name);
	if (now - last_summary_ms_ >= kSummaryIntervalMs) {
		last_summary_ms_ = now;
		logSummary(session_.stats(now), source_name);
	}
	return event;
}

void Meter::pause()
{
	std::lock_guard<std::mutex> lock(mutex_);
	session_.pause(nowMs());
	obs_log(LOG_INFO, "measurement paused");
}

void Meter::resume()
{
	std::lock_guard<std::mutex> lock(mutex_);
	session_.resume(nowMs());
	obs_log(LOG_INFO, "measurement resumed");
}

void Meter::reset()
{
	std::lock_guard<std::mutex> lock(mutex_);
	session_.reset(nowMs());
	obs_log(LOG_INFO, "measurement reset");
}

MeterSnapshot Meter::snapshot()
{
	std::lock_guard<std::mutex> lock(mutex_);
	const int64_t now = nowMs();
	MeterSnapshot snap;
	snap.stats = session_.stats(now);
	snap.has_frame = has_frame_;
	snap.last_frame_age_ms = has_frame_ ? now - last_frame_ms_ : 0;
	snap.status = status_;
	snap.scale = scale_;
	snap.source_name = source_name_;
	return snap;
}

// Gains are summarized periodically; everything unusual is logged as it happens.
void Meter::logEvent(ReadingEvent event, const HudReading &reading, const std::string &source_name)
{
	if (event != ReadingEvent::Baseline && event != ReadingEvent::LevelUp && event != ReadingEvent::Loss &&
	    event != ReadingEvent::Rejected)
		return;
	obs_log(LOG_INFO, "[%s] %s: exp=%s pct=%.2f lv=%d", source_name.c_str(), eventName(event),
		formatNumber(reading.exp).c_str(), reading.percent(), reading.level);
}

void Meter::logSummary(const SessionStats &s, const std::string &source_name)
{
	if (!s.has_reading)
		return;
	const std::string eta = s.eta_seconds ? formatDuration(*s.eta_seconds) : std::string("-");
	const std::string required = s.required_exp ? formatNumber(static_cast<int64_t>(*s.required_exp))
						    : std::string("?");
	obs_log(LOG_INFO,
		"[%s] session %s%s%s | %s EXP | %s EXP/min (last 5 min) | %s EXP/min avg | lv %d %.2f%% of %s | "
		"next level in %s",
		source_name.c_str(), formatDuration(s.active_ms / 1000.0).c_str(), s.running ? "" : " (paused)",
		s.hud_visible ? "" : " (HUD not visible)", formatNumber(s.session_exp).c_str(),
		s.rate_ready ? formatNumber(std::llround(s.exp_per_min)).c_str() : "-",
		s.rate_ready ? formatNumber(std::llround(s.session_exp_per_min)).c_str() : "-", s.level,
		s.percent_x100 / 100.0, required.c_str(), eta.c_str());
}

} // namespace expmeter
