#include "stats/session.hpp"

#include <doctest.h>

#include <cmath>
#include <cstdint>

template<> struct doctest::StringMaker<expmeter::ReadingEvent> {
	static String convert(expmeter::ReadingEvent event) { return expmeter::eventName(event); }
};

using namespace expmeter;

namespace {

constexpr int64_t kSecond = 1000;
constexpr int64_t kMinute = 60 * kSecond;
constexpr int64_t kReadInterval = 500;

// Total EXP of level 39, consistent with the live 1080p session below.
constexpr double kRequired39 = 266729.0;

int percentFor(int64_t exp, double required)
{
	return static_cast<int>(std::floor(exp * 10000.0 / required));
}

// The filter reads twice per second; a stable value is seen at least twice.
ReadingEvent feed(Session &s, int64_t t, int64_t exp, int pct, int level)
{
	s.onReading(t, exp, pct, level);
	return s.onReading(t + kReadInterval, exp, pct, level);
}

ReadingEvent feed(Session &s, int64_t t, int64_t exp, int level = 39, double required = kRequired39)
{
	return feed(s, t, exp, percentFor(exp, required), level);
}

} // namespace

TEST_CASE("replays the live 1080p session from the OBS log")
{
	// Readings logged by the filter on 2026-10-19, 23:45:48 to 23:46:17.
	struct Logged {
		int64_t t_ms;
		int64_t exp;
		int pct;
	};
	const Logged log[] = {
		{0, 75479, 2830},     {1033, 75500, 2831},  {3134, 75521, 2832},  {5200, 75553, 2833},
		{7800, 75585, 2834},  {8333, 75617, 2835},  {10366, 75619, 2836}, {10867, 75640, 2836},
		{11367, 75642, 2836}, {11900, 75663, 2837}, {13966, 75706, 2839}, {27033, 75708, 2839},
		{29133, 75751, 2840},
	};
	Session session;
	for (const auto &r : log) {
		CAPTURE(r.exp);
		const ReadingEvent e = feed(session, r.t_ms, r.exp, r.pct, 39);
		CHECK(e != ReadingEvent::Rejected);
	}
	const SessionStats s = session.stats(29133 + kReadInterval);
	CHECK(s.session_exp == 75751 - 75479);
	CHECK(s.lost_exp == 0);
	CHECK(s.level == 39);
	REQUIRE(s.required_exp);
	CHECK(*s.required_exp == doctest::Approx(kRequired39).epsilon(0.001));
}

TEST_CASE("a value must be read twice before it counts")
{
	Session session;
	CHECK(feed(session, 0, 75000) == ReadingEvent::Baseline);
	CHECK(session.onReading(2 * kSecond, 75100, percentFor(75100, kRequired39), 39) == ReadingEvent::Pending);
	CHECK(session.stats(2 * kSecond).session_exp == 0);
	CHECK(session.onReading(2 * kSecond + kReadInterval, 75100, percentFor(75100, kRequired39), 39) ==
	      ReadingEvent::Gain);
	CHECK(session.stats(3 * kSecond).session_exp == 100);
}

TEST_CASE("a misread digit is rejected even if read twice")
{
	Session session;
	feed(session, 0, 75751);
	// "76751" with the same 28.40%: implies a different level total, so it can't be real.
	CHECK(feed(session, 2 * kSecond, 76751, 2840, 39) == ReadingEvent::Rejected);
	CHECK(feed(session, 4 * kSecond, 75800) == ReadingEvent::Gain);
	CHECK(session.stats(5 * kSecond).session_exp == 75800 - 75751);
}

TEST_CASE("a value that keeps being read becomes the new baseline without counting")
{
	Session session;
	feed(session, 0, 75751);
	// Switched to another character (level total ~100,000), read consistently.
	CHECK(feed(session, 2 * kSecond, 12000, 1200, 39) == ReadingEvent::Rejected);
	CHECK(feed(session, 4 * kSecond, 12005, 1200, 39) == ReadingEvent::Rejected);
	CHECK(feed(session, 6 * kSecond, 12010, 1200, 39) == ReadingEvent::Baseline);
	CHECK(session.stats(7 * kSecond).session_exp == 0);
	CHECK(feed(session, 8 * kSecond, 12100, 1210, 39) == ReadingEvent::Gain);
	CHECK(session.stats(9 * kSecond).session_exp == 90);
}

TEST_CASE("a death is counted as a loss")
{
	Session session;
	feed(session, 0, 100000);
	feed(session, 2 * kSecond, 110000);
	CHECK(feed(session, 4 * kSecond, 83000) == ReadingEvent::Loss); // same level total
	const SessionStats s = session.stats(5 * kSecond);
	CHECK(s.gained_exp == 10000);
	CHECK(s.lost_exp == 27000);
	CHECK(s.session_exp == -17000);
	CHECK(s.losses == 1);
}

TEST_CASE("a level-up counts the EXP that crossed the level")
{
	constexpr double kRequired40 = 297000.0;
	SUBCASE("level digits readable")
	{
		Session session;
		feed(session, 0, 260000);
		feed(session, 2 * kSecond, 265000);
		CHECK(feed(session, 4 * kSecond, 1500, 40, kRequired40) == ReadingEvent::LevelUp);
		const SessionStats s = session.stats(5 * kSecond);
		CHECK(s.level == 40);
		CHECK(s.level_ups == 1);
		// 5,000 before, then (266,729 - 265,000) + 1,500 across the level.
		CHECK(s.session_exp == doctest::Approx(5000 + 1729 + 1500).epsilon(0.001));
	}
	SUBCASE("level digits unknown: detected from the % wrapping")
	{
		Session session;
		feed(session, 0, 260000, 0);
		feed(session, 2 * kSecond, 265000, 0);
		CHECK(feed(session, 4 * kSecond, 1500, 0, kRequired40) == ReadingEvent::LevelUp);
		CHECK(session.stats(5 * kSecond).session_exp == doctest::Approx(8229).epsilon(0.001));
	}
}

TEST_CASE("an unknown level on some frames does not block confirmation")
{
	Session session;
	feed(session, 0, 75000);
	session.onReading(2 * kSecond, 75100, percentFor(75100, kRequired39), 0);
	CHECK(session.onReading(2 * kSecond + kReadInterval, 75100, percentFor(75100, kRequired39), 39) ==
	      ReadingEvent::Gain);
	CHECK(session.stats(3 * kSecond).level == 39);
}

TEST_CASE("rates, required EXP and time to level")
{
	// 100 EXP every 2 seconds for 10 minutes = 3,000 EXP/min.
	Session session;
	int64_t exp = 100000;
	int64_t t = 0;
	feed(session, t, exp);
	for (int i = 0; i < 300; ++i) {
		t += 2 * kSecond;
		exp += 100;
		feed(session, t, exp);
	}
	const SessionStats s = session.stats(t + kReadInterval);
	CHECK(s.active_ms == doctest::Approx(10 * kMinute).epsilon(0.01));
	CHECK(s.session_exp == 30000);
	CHECK(s.rate_ready);
	CHECK(s.exp_per_min == doctest::Approx(3000).epsilon(0.02));
	CHECK(s.session_exp_per_min == doctest::Approx(3000).epsilon(0.02));
	CHECK(s.exp_per_hour == doctest::Approx(180000).epsilon(0.02));
	REQUIRE(s.required_exp);
	CHECK(*s.required_exp == doctest::Approx(kRequired39).epsilon(0.001));
	REQUIRE(s.eta_seconds);
	CHECK(*s.eta_seconds == doctest::Approx((kRequired39 - exp) / 3000.0 * 60.0).epsilon(0.02));
}

TEST_CASE("the window rate follows the recent pace")
{
	Session session;
	int64_t exp = 100000;
	int64_t t = 0;
	feed(session, t, exp);
	// 10 minutes at 3,000 EXP/min, then 5 minutes at 1,500 EXP/min.
	for (int i = 0; i < 300; ++i) {
		t += 2 * kSecond;
		exp += 100;
		feed(session, t, exp);
	}
	for (int i = 0; i < 150; ++i) {
		t += 2 * kSecond;
		exp += 50;
		feed(session, t, exp);
	}
	const SessionStats s = session.stats(t + kReadInterval);
	CHECK(s.exp_per_min == doctest::Approx(1500).epsilon(0.05));
	CHECK(s.session_exp_per_min == doctest::Approx(2500).epsilon(0.05));
}

TEST_CASE("time stops while the HUD is not visible")
{
	Session session;
	// 10 seconds of play, a reading every 2 seconds.
	for (int i = 0; i <= 5; ++i)
		feed(session, i * 2 * kSecond, 100000 + i * 20);
	CHECK(session.stats(10 * kSecond).active_ms == doctest::Approx(10 * kSecond).epsilon(0.1));

	// Cash shop for 2 minutes: frames arrive but the HUD is not found. The clock stops after
	// the 3 second timeout.
	for (int64_t t = 11 * kSecond; t < 131 * kSecond; t += kReadInterval)
		session.onNoReading(t);
	SessionStats s = session.stats(131 * kSecond);
	CHECK_FALSE(s.hud_visible);
	CHECK(s.active_ms == doctest::Approx(13 * kSecond).epsilon(0.1));

	// Back in game for 10 seconds: the clock resumes.
	for (int i = 0; i <= 5; ++i)
		feed(session, 131 * kSecond + i * 2 * kSecond, 100200 + i * 20);
	s = session.stats(141 * kSecond);
	CHECK(s.hud_visible);
	CHECK(s.active_ms == doctest::Approx(23 * kSecond).epsilon(0.1));
	CHECK(s.session_exp == 300);
}

TEST_CASE("pause, resume and reset")
{
	Session session;
	for (int i = 0; i <= 5; ++i)
		feed(session, i * 2 * kSecond, 100000 + i * 20); // +100 in 10 s
	session.pause(10 * kSecond);
	for (int i = 1; i <= 30; ++i)
		feed(session, 10 * kSecond + i * 2 * kSecond, 100100 + i * 20); // +600 while paused
	SessionStats s = session.stats(70 * kSecond);
	CHECK_FALSE(s.running);
	CHECK(s.active_ms < 11 * kSecond);
	CHECK(s.session_exp == 100);
	CHECK(s.exp == 100700); // still tracked, so resuming starts from here

	session.resume(70 * kSecond);
	feed(session, 72 * kSecond, 100750);
	CHECK(session.stats(73 * kSecond).session_exp == 150);

	session.reset(73 * kSecond); // new measurement, e.g. a new training spot
	s = session.stats(73 * kSecond);
	CHECK(s.session_exp == 0);
	CHECK(s.active_ms == 0);
	CHECK(s.exp == 100750); // current EXP is the new baseline

	feed(session, 74 * kSecond, 100800);
	CHECK(session.stats(75 * kSecond).session_exp == 50);
}

TEST_CASE("required EXP range from one reading")
{
	const RequiredExpRange r = requiredExpRange(75751, 2840);
	REQUIRE(r.bounded());
	CHECK(r.low <= kRequired39);
	CHECK(r.high >= kRequired39);
	CHECK(r.uncertainty() < 0.001);

	CHECK_FALSE(requiredExpRange(0, 0).bounded());
	CHECK_FALSE(requiredExpRange(120, 0).bounded());
	CHECK(requiredExpRange(120, 0).low >= 1200000);
}
