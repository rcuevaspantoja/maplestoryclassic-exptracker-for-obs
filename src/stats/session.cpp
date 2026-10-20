#include "session.hpp"

#include <algorithm>
#include <cmath>

namespace expmeter {

namespace {

constexpr double kRangeSlack = 1e-4;   // relative tolerance when intersecting ranges
constexpr int kWrapPercentDrop = 5000; // a 50+ point drop in % is a level-up, not a death
constexpr double kRequiredMaxUncertainty = 0.02;

RequiredExpRange intersect(const RequiredExpRange &a, const RequiredExpRange &b)
{
	RequiredExpRange out;
	out.low = std::max(a.low, b.low);
	if (a.bounded() && b.bounded())
		out.high = std::min(a.high, b.high);
	else
		out.high = a.bounded() ? a.high : b.high;
	return out;
}

bool consistent(const RequiredExpRange &a, const RequiredExpRange &b)
{
	const RequiredExpRange both = intersect(a, b);
	return !both.bounded() || both.low <= both.high * (1.0 + kRangeSlack);
}

} // namespace

RequiredExpRange requiredExpRange(int64_t exp, int percent_x100)
{
	// The HUD may round or truncate the percentage, so the true fraction is anywhere in
	// [percent - 0.005%, percent + 0.01%).
	RequiredExpRange range;
	if (exp <= 0)
		return range; // no information
	const double e = static_cast<double>(exp);
	const double frac_low = std::max(percent_x100 - 0.5, 0.0) / 10000.0;
	const double frac_high = std::min(percent_x100 + 1.0, 10000.0) / 10000.0;
	range.low = std::max(e / frac_high, percent_x100 >= 10000 ? e : e + 1.0);
	range.high = frac_low > 0.0 ? e / frac_low : 0.0;
	return range;
}

const char *eventName(ReadingEvent event)
{
	switch (event) {
	case ReadingEvent::Pending:
		return "pending";
	case ReadingEvent::Baseline:
		return "baseline";
	case ReadingEvent::NoChange:
		return "no-change";
	case ReadingEvent::Gain:
		return "gain";
	case ReadingEvent::Loss:
		return "loss";
	case ReadingEvent::LevelUp:
		return "level-up";
	case ReadingEvent::Rejected:
		return "rejected";
	}
	return "unknown";
}

bool Session::visibleAt(int64_t now_ms) const
{
	return last_seen_ms_ && now_ms - *last_seen_ms_ <= config_.hud_timeout_ms;
}

void Session::tick(int64_t now_ms)
{
	// Time between two observations counts if the clock runs, a baseline exists and the HUD
	// was seen recently (alt-tab, cash shop and loading screens pause it automatically).
	if (last_tick_ms_ && running_ && current_ && visibleAt(now_ms))
		active_ms_ += std::max<int64_t>(0, now_ms - *last_tick_ms_);
	last_tick_ms_ = now_ms;
}

ReadingEvent Session::onReading(int64_t now_ms, int64_t exp, int percent_x100, int level)
{
	tick(now_ms);
	last_seen_ms_ = now_ms;

	const Value value{exp, percent_x100, level};
	// The level digits may be unreadable on some frames; an unknown level matches any.
	const bool same = candidate_ && candidate_->exp == exp && candidate_->percent_x100 == percent_x100 &&
			  (candidate_->level == level || candidate_->level == 0 || level == 0);
	if (same) {
		++candidate_count_;
		candidate_->level = std::max(candidate_->level, level);
	} else {
		candidate_ = value;
		candidate_count_ = 1;
	}
	if (candidate_count_ < config_.confirmations)
		return ReadingEvent::Pending;
	if (candidate_count_ > config_.confirmations)
		return ReadingEvent::NoChange;
	return accept(*candidate_);
}

void Session::onNoReading(int64_t now_ms)
{
	tick(now_ms);
}

ReadingEvent Session::accept(const Value &read)
{
	const RequiredExpRange range = requiredExpRange(read.exp, read.percent_x100);
	if (!current_) {
		current_ = read;
		required_ = range;
		return ReadingEvent::Baseline;
	}

	const Value prev = *current_;
	Value value = read;
	if (value.level == 0)
		value.level = prev.level; // the level is optional; keep the last known one
	if (value == prev)
		return ReadingEvent::NoChange;

	const bool levels_known = read.level > 0 && prev.level > 0;
	const bool level_up_by_digits = levels_known && read.level == prev.level + 1;
	const bool level_changed_oddly = levels_known && read.level != prev.level && !level_up_by_digits;

	if (!level_up_by_digits && !level_changed_oddly && consistent(required_, range)) {
		// Same level: a gain, or a loss (death).
		required_ = intersect(required_, range);
		current_ = value;
		jump_.reset();
		const int64_t delta = value.exp - prev.exp;
		if (delta == 0)
			return ReadingEvent::NoChange;
		record(delta);
		return delta > 0 ? ReadingEvent::Gain : ReadingEvent::Loss;
	}

	// EXP fell and the implied level total grew (or the % wrapped): a level-up. With known
	// level digits that stayed the same, it can't be one.
	const bool same_level_digits = levels_known && read.level == prev.level;
	const bool wrapped = read.exp < prev.exp && ((required_.bounded() && range.low > required_.high) ||
						     read.percent_x100 + kWrapPercentDrop < prev.percent_x100);
	if (level_up_by_digits || (wrapped && !same_level_digits)) {
		const double to_next = std::max(0.0, required_.estimate() - static_cast<double>(prev.exp));
		record(std::llround(to_next) + read.exp);
		if (running_)
			++level_ups_;
		if (read.level == 0)
			value.level = prev.level > 0 ? prev.level + 1 : 0;
		current_ = value;
		required_ = range;
		jump_.reset();
		return ReadingEvent::LevelUp;
	}

	// Anything else is a misread, or a real jump such as a character switch. Count nothing;
	// adopt it as the new baseline only once it has been read consistently a few times.
	if (jump_ && jump_->level == value.level && consistent(jump_range_, range)) {
		++jump_count_;
		jump_range_ = intersect(jump_range_, range);
	} else {
		jump_count_ = 1;
		jump_range_ = range;
	}
	jump_ = value;
	if (jump_count_ >= config_.rebaseline_after) {
		current_ = value;
		required_ = jump_range_;
		jump_.reset();
		return ReadingEvent::Baseline;
	}
	return ReadingEvent::Rejected;
}

void Session::record(int64_t delta)
{
	// While paused the EXP keeps being tracked (so resuming starts from the right value) but
	// nothing is added to the measurement.
	if (!running_)
		return;
	if (delta > 0) {
		gained_ += delta;
	} else {
		lost_ -= delta;
		++losses_;
	}
	history_.push_back({active_ms_, gained_ - lost_});
}

int64_t Session::netAt(int64_t active_ms) const
{
	int64_t net = 0;
	for (const Point &p : history_) {
		if (p.active_ms > active_ms)
			break;
		net = p.net_exp;
	}
	return net;
}

void Session::pause(int64_t now_ms)
{
	tick(now_ms);
	running_ = false;
}

void Session::resume(int64_t now_ms)
{
	tick(now_ms);
	running_ = true;
}

void Session::reset(int64_t now_ms)
{
	tick(now_ms);
	active_ms_ = 0;
	gained_ = 0;
	lost_ = 0;
	level_ups_ = 0;
	losses_ = 0;
	history_.clear();
}

SessionStats Session::stats(int64_t now_ms) const
{
	SessionStats s;
	s.has_reading = current_.has_value();
	s.running = running_;
	s.hud_visible = visibleAt(now_ms);
	if (current_) {
		s.level = current_->level;
		s.exp = current_->exp;
		s.percent_x100 = current_->percent_x100;
	}

	s.active_ms = active_ms_;
	if (last_tick_ms_ && running_ && current_ && s.hud_visible)
		s.active_ms += std::max<int64_t>(0, now_ms - *last_tick_ms_);

	s.gained_exp = gained_;
	s.lost_exp = lost_;
	s.session_exp = gained_ - lost_;
	s.level_ups = level_ups_;
	s.losses = losses_;

	if (s.active_ms > 0) {
		const int64_t window_start = std::max<int64_t>(0, s.active_ms - config_.window_ms);
		const double window_min = (s.active_ms - window_start) / 60000.0;
		const int64_t base = window_start > 0 ? netAt(window_start) : 0;
		s.exp_per_min = (s.session_exp - base) / window_min;
		s.session_exp_per_min = s.session_exp / (s.active_ms / 60000.0);
		s.exp_per_hour = s.session_exp_per_min * 60.0;
	}
	s.rate_ready = s.active_ms >= config_.warmup_ms;

	if (current_ && required_.uncertainty() <= kRequiredMaxUncertainty)
		s.required_exp = required_.estimate();
	if (s.rate_ready && s.exp_per_min > 0.0 && s.required_exp) {
		const double remaining = std::max(0.0, *s.required_exp - static_cast<double>(s.exp));
		s.eta_seconds = remaining / s.exp_per_min * 60.0;
	}
	return s;
}

} // namespace expmeter
