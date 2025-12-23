#pragma once

/**
 * script.hpp - Script System Aggregate Header
 * 
 * Includes all script-related types:
 * - Script class for executable scripts
 * - ScriptEnvironment for Node.js isolate management
 * - ScriptEngine for top-level API
 * 
 * Note: Implementation files require Node.js headers for full definitions.
 */

#include "script/script_class.hpp"
#include "script/environment.hpp"
#include "script/engine.hpp"
