/**
 * obs-bindings.h - OBS JavaScript bindings
 * 
 * Exposes OBS APIs to JavaScript via V8.
 */

#pragma once

#include "obs-data.h"

#include <node/v8.h>

namespace obs_bindings {

/**
 * Initialize all OBS bindings on the global obs object.
 * Must be called after LoadEnvironment with V8 scopes active.
 */
void Initialize(v8::Isolate* isolate, v8::Local<v8::Context> context);

// Individual binding setup functions
void SetupSourceBindings(v8::Isolate* isolate, v8::Local<v8::Object> obs);
void SetupSceneBindings(v8::Isolate* isolate, v8::Local<v8::Object> obs);
void SetupFrontendBindings(v8::Isolate* isolate, v8::Local<v8::Object> obs);
void SetupCanvasBindings(v8::Isolate* isolate, v8::Local<v8::Object> obs);
void SetupEventBindings(v8::Isolate* isolate, v8::Local<v8::Object> obs);
void SetupSceneItemBindings(v8::Isolate* isolate, v8::Local<v8::Object> obs);
void SetupFilterBindings(v8::Isolate* isolate, v8::Local<v8::Object> obs);
void SetupTransitionBindings(v8::Isolate* isolate, v8::Local<v8::Object> obs);
void SetupOutputBindings(v8::Isolate* isolate, v8::Local<v8::Object> obs);
void SetupEncoderBindings(v8::Isolate* isolate, v8::Local<v8::Object> obs);
void SetupServiceBindings(v8::Isolate* isolate, v8::Local<v8::Object> obs);
void SetupDataBindings(v8::Isolate* isolate, v8::Local<v8::Object> obs);
void SetupPropertiesBindings(v8::Isolate* isolate, v8::Local<v8::Object> obs);
void SetupAudioBindings(v8::Isolate* isolate, v8::Local<v8::Object> obs);
void SetupHotkeyBindings(v8::Isolate* isolate, v8::Local<v8::Object> obs);
void SetupWebSocketBindings(v8::Isolate* isolate, v8::Local<v8::Object> obs);

// Utilities
obs_data_t* JSToObsData(v8::Isolate* isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> obj);
v8::Local<v8::Object> ObsDataToJS(v8::Isolate* isolate, v8::Local<v8::Context> context, obs_data_t* data);
void SetupModuleBindings(v8::Isolate* isolate, v8::Local<v8::Object> obs);

// Cleanup function (call on plugin unload)
void CleanupEventBindings();

} // namespace obs_bindings
