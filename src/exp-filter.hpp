#pragma once

// Registers the "EXP Meter" video filter. It leaves the image untouched and, a couple of
// times per second, reads the MapleStory Classic HUD from the bottom of the frame.
void registerExpFilter();
