/**
 * obs-frontend.cpp - OBS frontend API bindings
 * 
 * Exposes obs.frontend API to JavaScript.
 */

#include "obs-bindings.h"
#include <obs.h>
#include <obs-frontend-api.h>
#include <string>

namespace obs_bindings {

// ============================================================================
// Streaming
// ============================================================================

static void StreamingStart(const v8::FunctionCallbackInfo<v8::Value>& args) {
    obs_frontend_streaming_start();
    args.GetReturnValue().Set(true);
}

static void StreamingStop(const v8::FunctionCallbackInfo<v8::Value>& args) {
    obs_frontend_streaming_stop();
    args.GetReturnValue().Set(true);
}

static void StreamingIsActive(const v8::FunctionCallbackInfo<v8::Value>& args) {
    bool active = obs_frontend_streaming_active();
    args.GetReturnValue().Set(active);
}

// ============================================================================
// Recording
// ============================================================================

static void RecordingStart(const v8::FunctionCallbackInfo<v8::Value>& args) {
    obs_frontend_recording_start();
    args.GetReturnValue().Set(true);
}

static void RecordingStop(const v8::FunctionCallbackInfo<v8::Value>& args) {
    obs_frontend_recording_stop();
    args.GetReturnValue().Set(true);
}

static void RecordingIsActive(const v8::FunctionCallbackInfo<v8::Value>& args) {
    bool active = obs_frontend_recording_active();
    args.GetReturnValue().Set(active);
}

static void RecordingPause(const v8::FunctionCallbackInfo<v8::Value>& args) {
    obs_frontend_recording_pause(true);
    args.GetReturnValue().Set(true);
}

static void RecordingUnpause(const v8::FunctionCallbackInfo<v8::Value>& args) {
    obs_frontend_recording_pause(false);
    args.GetReturnValue().Set(true);
}

static void RecordingIsPaused(const v8::FunctionCallbackInfo<v8::Value>& args) {
    bool paused = obs_frontend_recording_paused();
    args.GetReturnValue().Set(paused);
}

// ============================================================================
// Virtual Camera
// ============================================================================

static void VirtualCamStart(const v8::FunctionCallbackInfo<v8::Value>& args) {
    obs_frontend_start_virtualcam();
    args.GetReturnValue().Set(true);
}

static void VirtualCamStop(const v8::FunctionCallbackInfo<v8::Value>& args) {
    obs_frontend_stop_virtualcam();
    args.GetReturnValue().Set(true);
}

static void VirtualCamIsActive(const v8::FunctionCallbackInfo<v8::Value>& args) {
    bool active = obs_frontend_virtualcam_active();
    args.GetReturnValue().Set(active);
}

// ============================================================================
// Scene Management
// ============================================================================

static void GetCurrentScene(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    
    obs_source_t* source = obs_frontend_get_current_scene();
    if (!source) {
        args.GetReturnValue().SetNull();
        return;
    }
    
    const char* name = obs_source_get_name(source);
    v8::Local<v8::String> result = v8::String::NewFromUtf8(isolate, name ? name : "").ToLocalChecked();
    obs_source_release(source);
    
    args.GetReturnValue().Set(result);
}

static void SetCurrentScene(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    
    if (args.Length() < 1 || !args[0]->IsString()) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    v8::String::Utf8Value name(isolate, args[0]);
    obs_source_t* source = obs_get_source_by_name(*name);
    
    if (!source) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    obs_frontend_set_current_scene(source);
    obs_source_release(source);
    args.GetReturnValue().Set(true);
}

static void GetPreviewScene(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    
    obs_source_t* source = obs_frontend_get_current_preview_scene();
    if (!source) {
        args.GetReturnValue().SetNull();
        return;
    }
    
    const char* name = obs_source_get_name(source);
    v8::Local<v8::String> result = v8::String::NewFromUtf8(isolate, name ? name : "").ToLocalChecked();
    obs_source_release(source);
    
    args.GetReturnValue().Set(result);
}

static void SetPreviewScene(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    
    if (args.Length() < 1 || !args[0]->IsString()) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    v8::String::Utf8Value name(isolate, args[0]);
    obs_source_t* source = obs_get_source_by_name(*name);
    
    if (!source) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    obs_frontend_set_current_preview_scene(source);
    obs_source_release(source);
    args.GetReturnValue().Set(true);
}

static void IsStudioMode(const v8::FunctionCallbackInfo<v8::Value>& args) {
    bool enabled = obs_frontend_preview_program_mode_active();
    args.GetReturnValue().Set(enabled);
}

static void SetStudioMode(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    
    if (args.Length() < 1 || !args[0]->IsBoolean()) {
        return;
    }
    
    bool enabled = args[0]->BooleanValue(isolate);
    obs_frontend_set_preview_program_mode(enabled);
}

static void GetTBarPosition(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    int posInt = obs_frontend_get_tbar_position();
    float position = (float)posInt / 1023.0f;
    if (position < 0.0f) position = 0.0f;
    if (position > 1.0f) position = 1.0f;
    args.GetReturnValue().Set(v8::Number::New(isolate, position));
}

static void SetTBarPosition(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    if (args.Length() < 1 || !args[0]->IsNumber()) {
        return;
    }
    
    float position = (float)args[0]->NumberValue(context).FromMaybe(0.0);
    if (position < 0.0f) position = 0.0f;
    if (position > 1.0f) position = 1.0f;
    
    int posInt = (int)(position * 1023.0f);
    obs_frontend_set_tbar_position(posInt);
}

// ============================================================================
// Replay Buffer
// ============================================================================

static void ReplayBufferStart(const v8::FunctionCallbackInfo<v8::Value>& args) {
    obs_frontend_replay_buffer_start();
    args.GetReturnValue().Set(true);
}

static void ReplayBufferStop(const v8::FunctionCallbackInfo<v8::Value>& args) {
    obs_frontend_replay_buffer_stop();
    args.GetReturnValue().Set(true);
}

static void ReplayBufferSave(const v8::FunctionCallbackInfo<v8::Value>& args) {
    obs_frontend_replay_buffer_save();
    args.GetReturnValue().Set(true);
}

static void ReplayBufferIsActive(const v8::FunctionCallbackInfo<v8::Value>& args) {
    bool active = obs_frontend_replay_buffer_active();
    args.GetReturnValue().Set(active);
}

void SetupFrontendBindings(v8::Isolate* isolate, v8::Local<v8::Object> obs) {
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    v8::Local<v8::Object> frontend = v8::Object::New(isolate);
    
    auto setFunc = [&](v8::Local<v8::Object> parent, const char* name, v8::FunctionCallback cb) {
        v8::Local<v8::Function> fn = v8::Function::New(context, cb).ToLocalChecked();
        parent->Set(context, 
            v8::String::NewFromUtf8(isolate, name).ToLocalChecked(),
            fn
        ).Check();
    };
    
    // Streaming sub-object
    v8::Local<v8::Object> streaming = v8::Object::New(isolate);
    setFunc(streaming, "start", StreamingStart);
    setFunc(streaming, "stop", StreamingStop);
    setFunc(streaming, "isActive", StreamingIsActive);
    frontend->Set(context, v8::String::NewFromUtf8(isolate, "streaming").ToLocalChecked(), streaming).Check();
    
    // Recording sub-object
    v8::Local<v8::Object> recording = v8::Object::New(isolate);
    setFunc(recording, "start", RecordingStart);
    setFunc(recording, "stop", RecordingStop);
    setFunc(recording, "isActive", RecordingIsActive);
    setFunc(recording, "pause", RecordingPause);
    setFunc(recording, "unpause", RecordingUnpause);
    setFunc(recording, "isPaused", RecordingIsPaused);
    frontend->Set(context, v8::String::NewFromUtf8(isolate, "recording").ToLocalChecked(), recording).Check();
    
    // Virtual camera sub-object
    v8::Local<v8::Object> virtualCam = v8::Object::New(isolate);
    setFunc(virtualCam, "start", VirtualCamStart);
    setFunc(virtualCam, "stop", VirtualCamStop);
    setFunc(virtualCam, "isActive", VirtualCamIsActive);
    frontend->Set(context, v8::String::NewFromUtf8(isolate, "virtualCam").ToLocalChecked(), virtualCam).Check();
    
    // Replay buffer sub-object
    v8::Local<v8::Object> replay = v8::Object::New(isolate);
    setFunc(replay, "start", ReplayBufferStart);
    setFunc(replay, "stop", ReplayBufferStop);
    setFunc(replay, "save", ReplayBufferSave);
    setFunc(replay, "isActive", ReplayBufferIsActive);
    frontend->Set(context, v8::String::NewFromUtf8(isolate, "replay").ToLocalChecked(), replay).Check();
    
    // Scene management
    setFunc(frontend, "getCurrentScene", GetCurrentScene);
    setFunc(frontend, "setCurrentScene", SetCurrentScene);
    setFunc(frontend, "getPreviewScene", GetPreviewScene);
    setFunc(frontend, "setPreviewScene", SetPreviewScene);
    setFunc(frontend, "isStudioMode", IsStudioMode);
    setFunc(frontend, "setStudioMode", SetStudioMode);
    setFunc(frontend, "getTBarPosition", GetTBarPosition);
    setFunc(frontend, "setTBarPosition", SetTBarPosition);
    
    obs->Set(context,
        v8::String::NewFromUtf8(isolate, "frontend").ToLocalChecked(),
        frontend
    ).Check();
}

} // namespace obs_bindings
