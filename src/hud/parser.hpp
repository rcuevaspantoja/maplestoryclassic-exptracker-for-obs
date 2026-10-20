#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace expmeter {

struct ExpValue {
	int64_t exp = 0;
	int percent_x100 = 0; // 28.00% -> 2800

	double percent() const { return percent_x100 / 100.0; }
};

// Parses the OCR text of the EXP readout, e.g. "74,660[28.00%]" or "0[0.00%]".
// Thousands separators may be ',' or '.', but groups must be well formed; anything
// that does not match the HUD format exactly is rejected.
std::optional<ExpValue> parseExpText(std::string_view text);

} // namespace expmeter
