#include "parser.hpp"

#include <cctype>

namespace expmeter {

namespace {

bool isDigit(char c)
{
	return std::isdigit(static_cast<unsigned char>(c)) != 0;
}

bool isSeparator(char c)
{
	return c == ',' || c == '.';
}

} // namespace

std::optional<ExpValue> parseExpText(std::string_view text)
{
	const size_t open = text.find('[');
	if (open == std::string_view::npos || open == 0)
		return std::nullopt;

	// EXP: 1-3 leading digits, then groups of exactly 3 digits after each separator.
	const std::string_view exp_part = text.substr(0, open);
	int64_t exp = 0;
	int group_len = 0;
	bool first_group = true;
	int total_digits = 0;
	for (char c : exp_part) {
		if (isDigit(c)) {
			exp = exp * 10 + (c - '0');
			++group_len;
			++total_digits;
			if (total_digits > 15)
				return std::nullopt;
		} else if (isSeparator(c)) {
			if (group_len == 0 || (first_group ? group_len > 3 : group_len != 3))
				return std::nullopt;
			first_group = false;
			group_len = 0;
		} else {
			return std::nullopt;
		}
	}
	if (group_len == 0 || (first_group ? false : group_len != 3))
		return std::nullopt;
	// Without separators, a leading zero only makes sense for the value 0 itself.
	if (exp_part.size() > 1 && exp_part[0] == '0')
		return std::nullopt;

	// Percentage: "[" 1-3 digits, '.' or ',', exactly 2 digits, "%]".
	std::string_view pct = text.substr(open + 1);
	if (pct.size() < 6 || pct.substr(pct.size() - 2) != "%]")
		return std::nullopt;
	pct.remove_suffix(2);
	const size_t dot = pct.find_first_of(".,");
	if (dot == std::string_view::npos || dot == 0 || dot > 3 || pct.size() - dot - 1 != 2)
		return std::nullopt;
	int whole = 0;
	for (size_t i = 0; i < dot; ++i) {
		if (!isDigit(pct[i]))
			return std::nullopt;
		whole = whole * 10 + (pct[i] - '0');
	}
	if (!isDigit(pct[dot + 1]) || !isDigit(pct[dot + 2]))
		return std::nullopt;
	const int percent_x100 = whole * 100 + (pct[dot + 1] - '0') * 10 + (pct[dot + 2] - '0');
	if (percent_x100 > 10000)
		return std::nullopt;

	return ExpValue{exp, percent_x100};
}

} // namespace expmeter
