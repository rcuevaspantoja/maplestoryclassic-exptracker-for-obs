#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace expmeter {

// Total EXP needed for the current level, narrowed down from the HUD readings. Every reading
// (exp, percent) bounds it, because the HUD shows percent = exp / required with 2 decimals.
struct RequiredExpRange {
	double low = 1.0;
	double high = 0.0; // <= 0 means unbounded

	bool bounded() const { return high > 0.0; }
	// Middle of the range, or the lower bound while there is no upper one.
	double estimate() const { return bounded() ? (low + high) / 2.0 : low; }
	// Width of the range relative to its value (0.01 = 1%); large when unbounded.
	double uncertainty() const { return bounded() ? (high - low) / low : 1e9; }
};

// The range of required EXP that is consistent with one HUD reading.
RequiredExpRange requiredExpRange(int64_t exp, int percent_x100);

struct SessionStats {
	bool has_reading = false; // a baseline reading was accepted
	bool running = false;     // not paused by the user
	bool hud_visible = false; // a reading arrived within the auto-pause timeout

	int level = 0; // 0 when unknown
	int64_t exp = 0;
	int percent_x100 = 0;

	int64_t active_ms = 0;   // measured time: running and HUD visible
	int64_t session_exp = 0; // net: gained - lost
	int64_t gained_exp = 0;
	int64_t lost_exp = 0;
	int level_ups = 0;
	int losses = 0; // EXP drops without a level change (deaths)

	bool rate_ready = false;            // enough measured time for stable rates
	double exp_per_min = 0.0;           // over the sliding window (last 5 minutes)
	double session_exp_per_min = 0.0;   // over the whole measurement
	double exp_per_hour = 0.0;          // session average, per hour
	std::optional<double> required_exp; // total EXP of the current level (estimated)
	std::optional<double> eta_seconds;  // time to the next level at exp_per_min
};

// What happened with a reading, for logging.
enum class ReadingEvent {
	Pending,  // waiting for the same value to be read again
	Baseline, // first accepted value, or a new one after a jump that persisted
	NoChange,
	Gain,
	Loss,
	LevelUp,
	Rejected, // inconsistent with the previous ones (likely misread); not counted
};

const char *eventName(ReadingEvent event);

// Turns HUD readings into session statistics. Time is passed in by the caller (milliseconds,
// any monotonic origin) so that whole sessions can be simulated in tests.
//
// A value only counts after it was read twice in a row, and only if it is consistent with
// the previous ones (see RequiredExpRange). A jump that is not a gain, death or level-up is
// rejected; if it keeps being read consistently, it becomes the new baseline (e.g. the
// streamer switched characters) without counting as EXP.
class Session {
public:
	struct Config {
		int64_t window_ms = 5 * 60 * 1000; // EXP/min sliding window
		int64_t hud_timeout_ms = 3000;     // no reading for this long pauses the clock
		int64_t warmup_ms = 60 * 1000;     // measured time before rates are "ready"
		int confirmations = 2;             // identical consecutive reads to accept a value
		int rebaseline_after = 3;          // confirmed inconsistent values before re-baselining
	};

	Session() : Session(Config{}) {}
	explicit Session(Config config) : config_(config) {}

	// A successful HUD read. Returns what the session did with it.
	ReadingEvent onReading(int64_t now_ms, int64_t exp, int percent_x100, int level);
	// A frame where the HUD was not found or not readable.
	void onNoReading(int64_t now_ms);

	// While paused neither time nor EXP is added to the measurement.
	void pause(int64_t now_ms);
	void resume(int64_t now_ms);
	// Starts a new measurement: clears totals and time, keeps the current EXP as baseline.
	void reset(int64_t now_ms);

	SessionStats stats(int64_t now_ms) const;

private:
	struct Value {
		int64_t exp = 0;
		int percent_x100 = 0;
		int level = 0;
		bool operator==(const Value &o) const
		{
			return exp == o.exp && percent_x100 == o.percent_x100 && level == o.level;
		}
	};
	struct Point {
		int64_t active_ms;
		int64_t net_exp;
	};

	void tick(int64_t now_ms);
	bool visibleAt(int64_t now_ms) const;
	ReadingEvent accept(const Value &value);
	void record(int64_t delta);
	int64_t netAt(int64_t active_ms) const;

	Config config_;
	bool running_ = true;

	// Debounce.
	std::optional<Value> candidate_;
	int candidate_count_ = 0;

	// Accepted state.
	std::optional<Value> current_;
	RequiredExpRange required_;
	std::optional<Value> jump_; // inconsistent value waiting to become the new baseline
	RequiredExpRange jump_range_;
	int jump_count_ = 0;

	// Time.
	std::optional<int64_t> last_tick_ms_;
	std::optional<int64_t> last_seen_ms_;
	int64_t active_ms_ = 0;

	// Totals for the current measurement.
	int64_t gained_ = 0;
	int64_t lost_ = 0;
	int level_ups_ = 0;
	int losses_ = 0;
	std::vector<Point> history_; // net EXP over measured time, for the sliding window
};

} // namespace expmeter
