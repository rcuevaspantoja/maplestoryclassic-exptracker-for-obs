#include "hud/parser.hpp"

#include <doctest.h>

using expmeter::parseExpText;

TEST_CASE("parses HUD values with thousands separators")
{
	auto v = parseExpText("74,660[28.00%]");
	REQUIRE(v);
	CHECK(v->exp == 74660);
	CHECK(v->percent_x100 == 2800);

	v = parseExpText("1,234,567[45.39%]");
	REQUIRE(v);
	CHECK(v->exp == 1234567);
	CHECK(v->percent_x100 == 4539);

	v = parseExpText("74.660[28,00%]");
	REQUIRE(v);
	CHECK(v->exp == 74660);
	CHECK(v->percent_x100 == 2800);
}

TEST_CASE("parses small values")
{
	auto v = parseExpText("0[0.00%]");
	REQUIRE(v);
	CHECK(v->exp == 0);
	CHECK(v->percent_x100 == 0);

	v = parseExpText("15[3.50%]");
	REQUIRE(v);
	CHECK(v->exp == 15);
	CHECK(v->percent_x100 == 350);

	v = parseExpText("999[100.00%]");
	REQUIRE(v);
	CHECK(v->percent_x100 == 10000);
}

TEST_CASE("rejects malformed text")
{
	CHECK_FALSE(parseExpText(""));
	CHECK_FALSE(parseExpText("[28.00%]"));         // no EXP
	CHECK_FALSE(parseExpText("74,66[28.00%]"));    // short group
	CHECK_FALSE(parseExpText("7466,0[28.00%]"));   // long first group
	CHECK_FALSE(parseExpText("74,,660[28.00%]"));  // empty group
	CHECK_FALSE(parseExpText("74,660,[28.00%]"));  // trailing separator
	CHECK_FALSE(parseExpText("074660[28.00%]"));   // leading zero
	CHECK_FALSE(parseExpText("74,660[28.0%]"));    // one decimal
	CHECK_FALSE(parseExpText("74,660[28.00]"));    // no percent sign
	CHECK_FALSE(parseExpText("74,660[28.00%"));    // no closing bracket
	CHECK_FALSE(parseExpText("74,660[100.01%]"));  // over 100%
	CHECK_FALSE(parseExpText("74,660[1000.00%]")); // too many digits
	CHECK_FALSE(parseExpText("74,6%0[28.00%]"));   // misread glyph
}
