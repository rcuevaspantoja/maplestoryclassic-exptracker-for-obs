#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace expmeter {

// 1234567 -> "1,234,567"
inline std::string formatNumber(int64_t value)
{
	std::string digits = std::to_string(value < 0 ? -value : value);
	for (int i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3)
		digits.insert(static_cast<size_t>(i), ",");
	return value < 0 ? "-" + digits : digits;
}

// 4350.0 -> "1:12:30", 75.0 -> "1:15"
inline std::string formatDuration(double seconds)
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

} // namespace expmeter
