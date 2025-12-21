/**
 * obs-node-ui.h - UI registration for OBS frontend
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Register plugin UI elements (menu items, docks, etc.)
 * Call this from obs_module_load() after Node.js is initialized.
 */
void obs_node_register_ui(void);

/**
 * Unregister plugin UI elements.
 * Call this from obs_module_unload() before Node.js shutdown.
 */
void obs_node_unregister_ui(void);

#ifdef __cplusplus
}
#endif
