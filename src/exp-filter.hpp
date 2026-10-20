#pragma once

inline constexpr const char *kExpFilterId = "exp_meter_filter";

// Registers the "EXP Meter" video filter. It leaves the image untouched and, a couple of
// times per second, reads the MapleStory Classic HUD from the bottom of the frame.
void registerExpFilter();
