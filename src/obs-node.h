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

#ifdef __cplusplus
#include <string>
#include <functional>

namespace obs_node {
    using ConsoleCallback = std::function<void(const std::string& type, const std::string& msg)>;
    void SetConsoleCallback(ConsoleCallback cb);
}
#endif