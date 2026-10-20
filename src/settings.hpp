#pragma once

#include <string>

namespace expmeter {

// User choices kept in the plugin's config folder (settings.json).
struct Settings {
	static constexpr int kDefaultPort = 48620;

	std::string overlay_theme = "default";
	int overlay_port = kDefaultPort;
};

Settings loadSettings();
void saveSettings(const Settings &settings);

} // namespace expmeter
