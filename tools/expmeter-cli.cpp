// Reads the MapleStory Classic HUD from image files and prints what it found.
// Usage: expmeter-cli <image> [<image> ...]

#include "hud/hud_reader.hpp"
#include "image_file.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>

int main(int argc, char **argv)
{
	if (argc < 2) {
		std::fprintf(stderr, "usage: %s <image> [<image> ...]\n", argv[0]);
		return 2;
	}

	int failures = 0;
	for (int i = 1; i < argc; ++i) {
		const auto image = expmeter::loadImageFile(argv[i]);
		if (!image) {
			const auto path = std::filesystem::absolute(argv[i]).string();
			if (!std::filesystem::exists(argv[i]))
				std::fprintf(stderr, "%s: file not found (looked for %s)\n", argv[i], path.c_str());
			else
				std::fprintf(stderr, "%s: not a readable PNG/BMP image\n", argv[i]);
			++failures;
			continue;
		}

		expmeter::HudReader reader;
		const auto start = std::chrono::steady_clock::now();
		const auto reading = reader.read(image->view());
		const auto ms =
			std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start)
				.count();

		std::printf("%s\n", argv[i]);
		if (reading.status == expmeter::ReadStatus::Ok) {
			std::printf("  exp=%lld pct=%.2f lv=", static_cast<long long>(reading.exp), reading.percent());
			if (reading.level > 0)
				std::printf("%d\n", reading.level);
			else
				std::printf("?\n");
		} else {
			std::printf("  status=%s\n", expmeter::statusName(reading.status));
			++failures;
		}
		if (reading.location) {
			std::printf("  label at (%d,%d) scale=%.2f match=%.3f\n", reading.location->x,
				    reading.location->y, reading.location->scale, reading.location->score);
		}
		if (!reading.raw_text.empty())
			std::printf("  text=\"%s\" confidence=%.2f\n", reading.raw_text.c_str(), reading.confidence);
		std::printf("  time=%lld ms\n", static_cast<long long>(ms));
	}
	return failures == 0 ? 0 : 1;
}
