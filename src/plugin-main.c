/*
obs-scripting-node
Copyright (C) 2026 psyirius psyirius@gmail.com

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
#include "obs-node.h"
#include "obs-node-ui.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

bool obs_module_load(void)
{
	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	
	// Initialize Node.js runtime
	obs_node_load();
	
	// Register UI elements (menu items, etc.)
	obs_node_register_ui();
	
	return true;
}

void obs_module_unload(void)
{
	// Unregister UI elements
	obs_node_unregister_ui();
	
	// Shutdown Node.js runtime
	obs_node_unload();
	
	obs_log(LOG_INFO, "plugin unloaded");
}

