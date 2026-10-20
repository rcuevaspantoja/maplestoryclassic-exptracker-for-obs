#include "hud/glyphs.hpp"
#include "hud/hud_reader.hpp"
#include "image_file.hpp"

#include <doctest.h>

#include <algorithm>
#include <string>

template<> struct doctest::StringMaker<expmeter::ReadStatus> {
	static String convert(expmeter::ReadStatus status) { return expmeter::statusName(status); }
};

using namespace expmeter;

namespace {

std::string reference(const std::string &name)
{
	return std::string(EXPMETER_REFERENCES_DIR) + "/" + name;
}

ImageFile load(const std::string &name)
{
	auto image = loadImageFile(reference(name));
	REQUIRE_MESSAGE(image, "cannot load " << name);
	return *image;
}

void setPixel(ImageFile &image, int x, int y, int r, int g, int b)
{
	uint8_t *p = &image.pixels[(static_cast<size_t>(y) * image.width + x) * 4];
	p[0] = static_cast<uint8_t>(r);
	p[1] = static_cast<uint8_t>(g);
	p[2] = static_cast<uint8_t>(b);
	p[3] = 255;
}

// Replaces the EXP readout of Screenshot_17.png (label template at 440,90) with `text`,
// drawn with the HUD font and spacing rules, so every glyph can be tested.
ImageFile withExpText(const std::string &text)
{
	ImageFile image = load("samples/Screenshot_17.png");
	const int label_x = 440, label_y = 90;
	const int text_x = label_x + 26;
	const int top = label_y + glyphs::kTextOffsetY;
	const int clean_x = label_x + glyphs::kTextOffsetX; // known blank column

	for (int y = top - 1; y <= top + glyphs::kExpGlyphHeight; ++y) {
		const uint8_t *bg = &image.pixels[(static_cast<size_t>(y) * image.width + clean_x) * 4];
		const int r = bg[0], g = bg[1], b = bg[2];
		for (int x = clean_x; x < clean_x + glyphs::kTextScanWidth && x < image.width; ++x)
			setPixel(image, x, y, r, g, b);
	}

	int pen = text_x;
	for (char ch : text) {
		const glyphs::ExpGlyph *glyph = nullptr;
		for (const auto &g : glyphs::kExpGlyphs)
			if (g.ch == ch)
				glyph = &g;
		REQUIRE_MESSAGE(glyph, "no glyph for " << ch);
		const bool green = ch == '[' || ch == ']';
		pen += glyph->left_bearing;
		const int w = static_cast<int>(glyph->rows[0].size());
		for (int r = 0; r < glyphs::kExpGlyphHeight; ++r)
			for (int c = 0; c < w; ++c)
				if (glyph->rows[r][c] == '#') {
					if (green)
						setPixel(image, pen + c, top + r, 160, 210, 40);
					else
						setPixel(image, pen + c, top + r, 255, 255, 255);
				}
		pen += w + glyph->right_bearing + 1;
	}
	return image;
}

// Area-averaged resize, like a capture that was scaled by a screenshot tool or a scene.
ImageFile resized(const ImageFile &src, double scale)
{
	constexpr int kSamples = 4;
	ImageFile out;
	out.width = static_cast<int>(src.width * scale);
	out.height = static_cast<int>(src.height * scale);
	out.pixels.resize(static_cast<size_t>(out.width) * out.height * 4);
	for (int y = 0; y < out.height; ++y) {
		for (int x = 0; x < out.width; ++x) {
			int sum[3] = {0, 0, 0};
			for (int sy = 0; sy < kSamples; ++sy) {
				for (int sx = 0; sx < kSamples; ++sx) {
					const int u = std::min(src.width - 1,
							       static_cast<int>((x + (sx + 0.5) / kSamples) / scale));
					const int v = std::min(src.height - 1,
							       static_cast<int>((y + (sy + 0.5) / kSamples) / scale));
					const uint8_t *p = &src.pixels[(static_cast<size_t>(v) * src.width + u) * 4];
					for (int c = 0; c < 3; ++c)
						sum[c] += p[c];
				}
			}
			setPixel(out, x, y, sum[0] / (kSamples * kSamples), sum[1] / (kSamples * kSamples),
				 sum[2] / (kSamples * kSamples));
		}
	}
	return out;
}

} // namespace

TEST_CASE("reads the 1366px game capture")
{
	const ImageFile image = load("game-capture.png");
	HudReader reader;
	const HudReading reading = reader.read(image.view());
	REQUIRE(reading.status == ReadStatus::Ok);
	CHECK(reading.exp == 74660);
	CHECK(reading.percent_x100 == 2800);
	CHECK(reading.level == 39);
	CHECK(reading.raw_text == "74,660[28.00%]");
}

TEST_CASE("reads the 800px HUD captures")
{
	HudReader reader;
	SUBCASE("level 39")
	{
		const ImageFile image = load("samples/Screenshot_17.png");
		const HudReading reading = reader.read(image.view());
		REQUIRE(reading.status == ReadStatus::Ok);
		CHECK(reading.exp == 74660);
		CHECK(reading.percent_x100 == 2800);
		CHECK(reading.level == 39);
	}
	SUBCASE("level 1, zero EXP")
	{
		const ImageFile image = load("samples/Screenshot_16.png");
		const HudReading reading = reader.read(image.view());
		REQUIRE(reading.status == ReadStatus::Ok);
		CHECK(reading.exp == 0);
		CHECK(reading.percent_x100 == 0);
		CHECK(reading.level == 1);
	}
}

TEST_CASE("reads a later capture of the same character")
{
	for (const char *name : {"samples/Screenshot_18.png", "samples/Screenshot_19.png"}) {
		CAPTURE(name);
		const ImageFile image = load(name);
		HudReader reader;
		const HudReading reading = reader.read(image.view());
		REQUIRE(reading.status == ReadStatus::Ok);
		CHECK(reading.exp == 74710);
		CHECK(reading.percent_x100 == 2801);
		CHECK(reading.level == 39);
	}
}

TEST_CASE("reads a tight crop of the EXP bar without the level")
{
	const ImageFile image = load("exp_bar.png");
	HudReader reader;
	const HudReading reading = reader.read(image.view());
	REQUIRE(reading.status == ReadStatus::Ok);
	CHECK(reading.exp == 74660);
	CHECK(reading.percent_x100 == 2800);
	CHECK(reading.level == 0);
}

TEST_CASE("reads the game stretched to 1920x1080")
{
	// MapleStory Classic stretches its 1366x768 canvas by ~1.406 at 1080p.
	const ImageFile image = load("samples/Screenshot 2026-10-19 23-30-48.png");
	HudReader reader;
	const HudReading reading = reader.read(image.view());
	CHECK(reading.raw_text == "74,965[28.11%]");
	REQUIRE(reading.status == ReadStatus::Ok);
	CHECK(reading.exp == 74965);
	CHECK(reading.percent_x100 == 2811);
	CHECK(reading.level == 39);
	REQUIRE(reading.location);
	CHECK(reading.location->scale == doctest::Approx(1.406).epsilon(0.01));

	// The next frame reuses the refined location and still reads.
	const HudReading again = reader.read(image.view(), false);
	CHECK(again.status == ReadStatus::Ok);
	CHECK(again.exp == 74965);
}

TEST_CASE("reads a native capture stretched like the game does at 1080p")
{
	// Screenshot_19 is the 1366px canvas; the game draws it 1920 / 1366 = 1.4056x larger.
	const ImageFile image = resized(load("samples/Screenshot_19.png"), 1920.0 / 1366.0);
	HudReader reader;
	const HudReading reading = reader.read(image.view());
	CHECK(reading.raw_text == "74,710[28.01%]");
	REQUIRE(reading.status == ReadStatus::Ok);
	CHECK(reading.exp == 74710);
	CHECK(reading.percent_x100 == 2801);
	CHECK(reading.level == 39);
}

TEST_CASE("reads other upscaled captures")
{
	// Only 1920x1080 (1.406x) is a supported setup; these check the method is not tuned to
	// a single scale. Stretches below ~1.4x (e.g. 1.25x) are not reliable yet.
	const ImageFile native = load("samples/Screenshot_17.png");
	for (double scale : {1.5, 1.75, 2.0}) {
		CAPTURE(scale);
		const ImageFile image = resized(native, scale);
		HudReader reader;
		const HudReading reading = reader.read(image.view());
		CHECK(reading.raw_text == "74,660[28.00%]");
		REQUIRE(reading.status == ReadStatus::Ok);
		CHECK(reading.exp == 74660);
		CHECK(reading.percent_x100 == 2800);
		// When the digits are not clear enough the level is unknown, never a wrong number.
		CHECK((reading.level == 39 || reading.level == 0));
	}
}

TEST_CASE("a shrunk capture is detected but not read")
{
	const ImageFile image = resized(load("samples/Screenshot_17.png"), 0.75);
	HudReader reader;
	const HudReading reading = reader.read(image.view());
	CHECK(reading.status == ReadStatus::UnsupportedScale);
	REQUIRE(reading.location);
	CHECK(reading.location->scale == doctest::Approx(0.75).epsilon(0.1));
}

TEST_CASE("a known scale is found again without the all-scales search")
{
	const ImageFile image = load("samples/Screenshot 2026-10-19 23-30-48.png");
	HudReader reader;
	REQUIRE(reader.read(image.view()).status == ReadStatus::Ok);

	// HUD hidden (e.g. cash shop), then back: no all-scales search is allowed.
	ImageFile blank = image;
	for (size_t i = 0; i < blank.pixels.size(); i += 4)
		blank.pixels[i] = blank.pixels[i + 1] = blank.pixels[i + 2] = 20;
	CHECK(reader.read(blank.view(), false).status == ReadStatus::HudNotFound);
	CHECK(reader.read(image.view(), false).status == ReadStatus::Ok);
}

TEST_CASE("an image without HUD is not read")
{
	ImageFile image = load("samples/Screenshot_17.png");
	for (size_t i = 0; i < image.pixels.size(); i += 4) {
		image.pixels[i] = 30;
		image.pixels[i + 1] = 30;
		image.pixels[i + 2] = 40;
	}
	HudReader reader;
	CHECK(reader.read(image.view()).status == ReadStatus::HudNotFound);
}

TEST_CASE("reads every digit and separator (synthetic)")
{
	const char *cases[][3] = {
		// text, exp, percent_x100
		{"1,234,567[45.39%]", "1234567", "4539"},
		{"9,876,543,210[99.99%]", "9876543210", "9999"},
		{"35[0.15%]", "35", "15"},
		{"2,147,483,647[100.00%]", "2147483647", "10000"},
		{"505,050[50.50%]", "505050", "5050"},
		{"119,191[11.91%]", "119191", "1191"},
	};
	for (const auto &c : cases) {
		CAPTURE(c[0]);
		const ImageFile image = withExpText(c[0]);
		HudReader reader;
		const HudReading reading = reader.read(image.view());
		CHECK(reading.raw_text == c[0]);
		REQUIRE(reading.status == ReadStatus::Ok);
		CHECK(reading.exp == std::stoll(c[1]));
		CHECK(reading.percent_x100 == std::stoi(c[2]));
		CHECK(reading.level == 39);
	}
}

TEST_CASE("reuses the previous location on the next frame")
{
	const ImageFile image = load("game-capture.png");
	HudReader reader;
	const HudReading first = reader.read(image.view());
	const HudReading second = reader.read(image.view());
	REQUIRE(first.location);
	REQUIRE(second.location);
	CHECK(second.location->x == first.location->x);
	CHECK(second.location->y == first.location->y);
	CHECK(second.status == ReadStatus::Ok);
}
