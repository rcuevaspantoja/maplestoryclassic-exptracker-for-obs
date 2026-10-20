#include "settings.hpp"

#include <obs-module.h>
#include <util/platform.h>

namespace expmeter {

Settings loadSettings()
{
	Settings settings;
	char *path = obs_module_config_path("settings.json");
	obs_data_t *data = path ? obs_data_create_from_json_file_safe(path, "bak") : nullptr;
	bfree(path);
	if (!data)
		return settings;

	const std::string theme = obs_data_get_string(data, "overlay_theme");
	if (!theme.empty())
		settings.overlay_theme = theme;
	const long long port = obs_data_get_int(data, "overlay_port");
	if (port >= 1024 && port <= 65535)
		settings.overlay_port = static_cast<int>(port);
	obs_data_release(data);
	return settings;
}

void saveSettings(const Settings &settings)
{
	char *dir = obs_module_config_path("");
	if (dir)
		os_mkdirs(dir);
	bfree(dir);
	char *path = obs_module_config_path("settings.json");
	if (!path)
		return;
	obs_data_t *data = obs_data_create();
	obs_data_set_string(data, "overlay_theme", settings.overlay_theme.c_str());
	obs_data_set_int(data, "overlay_port", settings.overlay_port);
	obs_data_save_json_safe(data, path, "tmp", "bak");
	obs_data_release(data);
	bfree(path);
}

} // namespace expmeter
