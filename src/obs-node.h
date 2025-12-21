#pragma once

// OBS headers
#include <obs-module.h>
#include <plugin-support.h>

#ifdef __cplusplus
extern "C" {
#endif

void obs_node_load(void);
void obs_node_unload(void);

#ifdef __cplusplus
}
#endif