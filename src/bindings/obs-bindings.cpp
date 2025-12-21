/**
 * obs-bindings.cpp - Central binding registration
 */

#include "obs-bindings.h"
#include <obs.h>

namespace obs_bindings {

/**
 * Get or create the global 'obs' object
 */
static v8::Local<v8::Object> GetObsObject(v8::Isolate* isolate, v8::Local<v8::Context> context) {
    v8::Local<v8::Object> global = context->Global();
    v8::Local<v8::String> key = v8::String::NewFromUtf8(isolate, "obs").ToLocalChecked();
    
    v8::Local<v8::Value> obsValue;
    if (!global->Get(context, key).ToLocal(&obsValue) || !obsValue->IsObject()) {
        // Create obs object if it doesn't exist
        v8::Local<v8::Object> obs = v8::Object::New(isolate);
        global->Set(context, key, obs).Check();
        return obs;
    }
    
    return obsValue.As<v8::Object>();
}

void Initialize(v8::Isolate* isolate, v8::Local<v8::Context> context) {
    v8::Local<v8::Object> obs = GetObsObject(isolate, context);
    
    // Setup all binding modules
    SetupSourceBindings(isolate, obs);
    SetupSceneBindings(isolate, obs);
    SetupFrontendBindings(isolate, obs);
    SetupCanvasBindings(isolate, obs);
    SetupEventBindings(isolate, obs);
    SetupSceneItemBindings(isolate, obs);
    SetupFilterBindings(isolate, obs);
    SetupTransitionBindings(isolate, obs);
    SetupOutputBindings(isolate, obs);
    SetupEncoderBindings(isolate, obs);
    SetupServiceBindings(isolate, obs);
    SetupDataBindings(isolate, obs);
    SetupPropertiesBindings(isolate, obs);
    SetupAudioBindings(isolate, obs);
    SetupHotkeyBindings(isolate, obs);
    SetupWebSocketBindings(isolate, obs);
    SetupModuleBindings(isolate, obs);
}

} // namespace obs_bindings
