/*
EXP Meter for MapleStory Classic
Copyright (C) 2026 Rodolfo

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <obs-module.h>
#include <plugin-support.h>

#include "exp-filter.hpp"
#include "ui/exp-dock.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

MODULE_EXPORT const char *obs_module_name(void)
{
	return obs_module_text("EXPMeter.Name");
}

MODULE_EXPORT const char *obs_module_description(void)
{
	return obs_module_text("EXPMeter.Description");
}

bool obs_module_load(void)
{
	registerExpFilter();
	obs_log(LOG_INFO, "EXP Meter loaded (version %s)", PLUGIN_VERSION);
	return true;
}

// Docks need the OBS main window, which is ready once all modules are loaded.
void obs_module_post_load(void)
{
	registerExpDock();
}

void obs_module_unload(void)
{
	unregisterExpDock();
	obs_log(LOG_INFO, "EXP Meter unloaded");
}
