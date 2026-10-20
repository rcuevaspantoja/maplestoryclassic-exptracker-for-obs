#include "exp-filter.hpp"

#include "hud/hud_reader.hpp"
#include "stats/session.hpp"

#include <obs-module.h>
#include <plugin-support.h>
#include <graphics/vec4.h>
#include <util/platform.h>

#include <algorithm>
#include <cmath>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr uint64_t kReadIntervalNs = 500'000'000ULL;     // while the HUD is visible
constexpr uint64_t kSearchIntervalNs = 2'000'000'000ULL; // while it is not (full searches cost more)
constexpr uint32_t kMaxBandHeight = 300;                 // the HUD lives in the bottom ~100px
constexpr uint64_t kScaleSearchIntervalNs = 10'000'000'000ULL;
constexpr int64_t kSummaryIntervalMs = 30'000;

// 1234567 -> "1,234,567"
std::string formatNumber(int64_t value)
{
	std::string digits = std::to_string(value < 0 ? -value : value);
	for (int i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3)
		digits.insert(static_cast<size_t>(i), ",");
	return value < 0 ? "-" + digits : digits;
}

// 4350.0 -> "1:12:30", 75.0 -> "1:15"
std::string formatDuration(double seconds)
{
	const int64_t total = std::max<int64_t>(0, std::llround(seconds));
	char out[32];
	if (total >= 3600)
		std::snprintf(out, sizeof(out), "%lld:%02lld:%02lld", static_cast<long long>(total / 3600),
			      static_cast<long long>(total / 60 % 60), static_cast<long long>(total % 60));
	else
		std::snprintf(out, sizeof(out), "%lld:%02lld", static_cast<long long>(total / 60),
			      static_cast<long long>(total % 60));
	return out;
}

// One copied strip from the bottom of the frame, handed to the worker thread.
struct Frame {
	std::vector<uint8_t> pixels; // RGBA, tightly packed
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t offset_y = 0; // first frame row of the strip
};

class ExpFilter {
public:
	explicit ExpFilter(obs_source_t *context) : context_(context), worker_([this] { workerLoop(); }) {}

	~ExpFilter()
	{
		{
			std::lock_guard<std::mutex> lock(mutex_);
			stop_ = true;
		}
		cv_.notify_one();
		worker_.join();

		obs_enter_graphics();
		gs_stagesurface_destroy(stagesurf_);
		gs_texrender_destroy(texrender_);
		obs_leave_graphics();
	}

	void render()
	{
		obs_source_skip_video_filter(context_);

		obs_source_t *target = obs_filter_get_target(context_);
		if (!target)
			return;
		const uint32_t width = obs_source_get_base_width(target);
		const uint32_t height = obs_source_get_base_height(target);
		if (!width || !height)
			return;

		// The copy staged on a previous frame is ready by now; mapping it doesn't stall the GPU.
		collectStaged();

		const uint64_t now = os_gettime_ns();
		const uint64_t interval = hud_visible_ ? kReadIntervalNs : kSearchIntervalNs;
		if (staged_ || busy_ || now - last_capture_ns_ < interval)
			return;
		last_capture_ns_ = now;
		captureBand(target, width, height);
	}

private:
	void captureBand(obs_source_t *target, uint32_t width, uint32_t height)
	{
		const uint32_t band_height = std::min(height, kMaxBandHeight);
		const uint32_t offset_y = height - band_height;

		if (!texrender_)
			texrender_ = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
		gs_texrender_reset(texrender_);
		if (!gs_texrender_begin(texrender_, width, band_height))
			return;

		struct vec4 clear;
		vec4_zero(&clear);
		gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
		gs_ortho(0.0f, static_cast<float>(width), static_cast<float>(offset_y), static_cast<float>(height),
			 -100.0f, 100.0f);
		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
		obs_source_video_render(target);
		gs_blend_state_pop();
		gs_texrender_end(texrender_);

		if (!stagesurf_ || stage_width_ != width || stage_height_ != band_height) {
			gs_stagesurface_destroy(stagesurf_);
			stagesurf_ = gs_stagesurface_create(width, band_height, GS_RGBA);
			stage_width_ = width;
			stage_height_ = band_height;
		}
		if (!stagesurf_)
			return;
		gs_stage_texture(stagesurf_, gs_texrender_get_texture(texrender_));
		staged_ = true;
		staged_offset_y_ = offset_y;
	}

	void collectStaged()
	{
		if (!staged_)
			return;
		staged_ = false;

		uint8_t *data = nullptr;
		uint32_t linesize = 0;
		if (!gs_stagesurface_map(stagesurf_, &data, &linesize))
			return;
		Frame frame;
		frame.width = stage_width_;
		frame.height = stage_height_;
		frame.offset_y = staged_offset_y_;
		const size_t row_bytes = static_cast<size_t>(frame.width) * 4;
		frame.pixels.resize(row_bytes * frame.height);
		for (uint32_t y = 0; y < frame.height; ++y)
			std::memcpy(&frame.pixels[y * row_bytes], data + static_cast<size_t>(y) * linesize, row_bytes);
		gs_stagesurface_unmap(stagesurf_);

		{
			std::lock_guard<std::mutex> lock(mutex_);
			job_ = std::move(frame);
			busy_ = true;
		}
		cv_.notify_one();
	}

	void workerLoop()
	{
		for (;;) {
			Frame frame;
			{
				std::unique_lock<std::mutex> lock(mutex_);
				cv_.wait(lock, [this] { return stop_ || job_.has_value(); });
				if (stop_)
					return;
				frame = std::move(*job_);
				job_.reset();
			}
			process(frame);
			busy_ = false;
		}
	}

	void process(const Frame &frame)
	{
		const expmeter::ImageView view{frame.pixels.data(), static_cast<int>(frame.width),
					       static_cast<int>(frame.height), static_cast<int>(frame.width * 4),
					       expmeter::PixelFormat::RGBA};
		const uint64_t start = os_gettime_ns();
		// Searching every scale costs up to ~1 s on a 1080p strip; do it at most every 10 s.
		const bool allow_scale_search = start - last_scale_search_ns_ >= kScaleSearchIntervalNs;
		if (allow_scale_search)
			last_scale_search_ns_ = start;
		const expmeter::HudReading reading = reader_.read(view, allow_scale_search);
		const double ms = (os_gettime_ns() - start) / 1e6;
		hud_visible_ = reading.status != expmeter::ReadStatus::HudNotFound;

		const int64_t now_ms = static_cast<int64_t>(start / 1'000'000);
		expmeter::ReadingEvent event = expmeter::ReadingEvent::NoChange;
		if (reading.status == expmeter::ReadStatus::Ok)
			event = session_.onReading(now_ms, reading.exp, reading.percent_x100, reading.level);
		else
			session_.onNoReading(now_ms);

		logStatusChange(reading, frame, ms);
		logEvent(event, reading);
		if (now_ms - last_summary_ms_ >= kSummaryIntervalMs) {
			last_summary_ms_ = now_ms;
			logSummary(session_.stats(now_ms));
		}
	}

	// Logs when the reader starts or stops seeing the HUD, not every reading.
	void logStatusChange(const expmeter::HudReading &reading, const Frame &frame, double ms)
	{
		const std::string status = expmeter::statusName(reading.status);
		if (status == last_status_)
			return;
		last_status_ = status;
		if (reading.location) {
			obs_log(LOG_INFO, "[%s] %s (HUD at %.1f,%.1f scale %.2f, %.1f ms)",
				obs_source_get_name(context_), status.c_str(), reading.location->x,
				reading.location->y + frame.offset_y, reading.location->scale, ms);
		} else {
			obs_log(LOG_INFO, "[%s] %s (%.1f ms)", obs_source_get_name(context_), status.c_str(), ms);
		}
	}

	// Gains are summarized periodically; everything unusual is logged as it happens.
	void logEvent(expmeter::ReadingEvent event, const expmeter::HudReading &reading)
	{
		using expmeter::ReadingEvent;
		if (event != ReadingEvent::Baseline && event != ReadingEvent::LevelUp && event != ReadingEvent::Loss &&
		    event != ReadingEvent::Rejected)
			return;
		obs_log(LOG_INFO, "[%s] %s: exp=%s pct=%.2f lv=%d", obs_source_get_name(context_),
			expmeter::eventName(event), formatNumber(reading.exp).c_str(), reading.percent(),
			reading.level);
	}

	void logSummary(const expmeter::SessionStats &s)
	{
		if (!s.has_reading)
			return;
		const std::string eta = s.eta_seconds ? formatDuration(*s.eta_seconds) : std::string("-");
		const std::string required = s.required_exp ? formatNumber(static_cast<int64_t>(*s.required_exp))
							    : std::string("?");
		obs_log(LOG_INFO,
			"[%s] session %s%s | %s EXP | %s EXP/min (last 5 min) | %s EXP/min avg | lv %d %.2f%% of %s | "
			"next level in %s",
			obs_source_get_name(context_), formatDuration(s.active_ms / 1000.0).c_str(),
			s.hud_visible ? "" : " (paused: HUD not visible)", formatNumber(s.session_exp).c_str(),
			s.rate_ready ? formatNumber(std::llround(s.exp_per_min)).c_str() : "-",
			s.rate_ready ? formatNumber(std::llround(s.session_exp_per_min)).c_str() : "-", s.level,
			s.percent_x100 / 100.0, required.c_str(), eta.c_str());
	}

	obs_source_t *context_;

	// Graphics thread only.
	gs_texrender_t *texrender_ = nullptr;
	gs_stagesurf_t *stagesurf_ = nullptr;
	uint32_t stage_width_ = 0;
	uint32_t stage_height_ = 0;
	uint32_t staged_offset_y_ = 0;
	bool staged_ = false;
	uint64_t last_capture_ns_ = 0;

	// Shared with the worker.
	std::mutex mutex_;
	std::condition_variable cv_;
	std::optional<Frame> job_;
	bool stop_ = false;
	std::atomic<bool> busy_{false};
	std::atomic<bool> hud_visible_{false};

	// Worker thread only.
	expmeter::HudReader reader_;
	expmeter::Session session_;
	std::string last_status_;
	int64_t last_summary_ms_ = 0;
	uint64_t last_scale_search_ns_ = 0;

	std::thread worker_; // last, so everything above exists before the thread starts
};

const char *filterName(void *)
{
	return obs_module_text("EXPMeter.Filter");
}

void *filterCreate(obs_data_t *, obs_source_t *source)
{
	return new ExpFilter(source);
}

void filterDestroy(void *data)
{
	delete static_cast<ExpFilter *>(data);
}

void filterRender(void *data, gs_effect_t *)
{
	static_cast<ExpFilter *>(data)->render();
}

} // namespace

void registerExpFilter()
{
	obs_source_info info = {};
	info.id = "exp_meter_filter";
	info.type = OBS_SOURCE_TYPE_FILTER;
	info.output_flags = OBS_SOURCE_VIDEO;
	info.get_name = filterName;
	info.create = filterCreate;
	info.destroy = filterDestroy;
	info.video_render = filterRender;
	obs_register_source(&info);
}
