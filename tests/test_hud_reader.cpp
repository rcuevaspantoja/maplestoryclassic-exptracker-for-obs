#include "hud/glyphs.hpp"
#include "hud/hud_reader.hpp"
#include "image_file.hpp"

#include <doctest.h>

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

TEST_CASE("a downscaled capture is detected but not read")
{
	const ImageFile image = load("samples/Screenshot_15.png");
	HudReader reader;
	const HudReading reading = reader.read(image.view());
	CHECK(reading.status == ReadStatus::UnsupportedScale);
	REQUIRE(reading.location);
	CHECK(reading.location->scale < 0.9);
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
