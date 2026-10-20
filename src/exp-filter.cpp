#include "exp-filter.hpp"

#include "hud/hud_reader.hpp"

#include <obs-module.h>
#include <plugin-support.h>
#include <graphics/vec4.h>
#include <util/platform.h>

#include <algorithm>
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

		// Log only when something changes, so the log stays readable during long sessions.
		char summary[160];
		if (reading.status == expmeter::ReadStatus::Ok) {
			char level[8] = "?";
			if (reading.level > 0)
				std::snprintf(level, sizeof(level), "%d", reading.level);
			std::snprintf(summary, sizeof(summary), "exp=%lld pct=%.2f lv=%s",
				      static_cast<long long>(reading.exp), reading.percent(), level);
		} else if (reading.status == expmeter::ReadStatus::Unreadable) {
			std::snprintf(summary, sizeof(summary), "unreadable text=\"%s\"", reading.raw_text.c_str());
		} else {
			std::snprintf(summary, sizeof(summary), "%s", expmeter::statusName(reading.status));
		}
		if (summary == last_summary_)
			return;
		last_summary_ = summary;

		if (reading.location) {
			obs_log(LOG_INFO, "[%s] %s (HUD at %.1f,%.1f scale %.2f, %.1f ms)",
				obs_source_get_name(context_), summary, reading.location->x,
				reading.location->y + frame.offset_y, reading.location->scale, ms);
		} else {
			obs_log(LOG_INFO, "[%s] %s (%.1f ms)", obs_source_get_name(context_), summary, ms);
		}
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
	std::string last_summary_;
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
