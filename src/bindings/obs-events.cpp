/**
 * obs-events.cpp - OBS event/callback bindings
 * 
 * Exposes obs.events API to JavaScript for subscribing to OBS events.
 */

#include "obs-bindings.h"
#include <obs.h>
#include <obs-frontend-api.h>
#include <vector>
#include <string>
#include <mutex>
#include <queue>
#include <unordered_map>

namespace obs_bindings {

// Store for JavaScript callbacks - includes persistent context
struct EventCallback {
    v8::Global<v8::Function> callback;
    v8::Global<v8::Context> context;
    v8::Isolate* isolate;
};

// Pending event to be processed
struct PendingEvent {
    std::string eventName;
    std::vector<std::string> args;
};

static std::mutex g_eventMutex;
static std::unordered_map<std::string, std::vector<EventCallback>> g_eventCallbacks;
static std::queue<PendingEvent> g_pendingEvents;
static bool g_frontendCallbackRegistered = false;

// Helper to invoke callbacks - must be called from V8 thread
static void ProcessPendingEvents() {
    std::lock_guard<std::mutex> lock(g_eventMutex);
    
    while (!g_pendingEvents.empty()) {
        PendingEvent event = std::move(g_pendingEvents.front());
        g_pendingEvents.pop();
        
        auto it = g_eventCallbacks.find(event.eventName);
        if (it == g_eventCallbacks.end()) continue;
        
        for (auto& cb : it->second) {
            if (!cb.isolate || cb.callback.IsEmpty() || cb.context.IsEmpty()) continue;
            
            v8::Locker locker(cb.isolate);
            v8::Isolate::Scope isolate_scope(cb.isolate);
            v8::HandleScope handle_scope(cb.isolate);
            v8::Local<v8::Context> context = cb.context.Get(cb.isolate);
            v8::Context::Scope context_scope(context);
            
            v8::Local<v8::Function> fn = cb.callback.Get(cb.isolate);
            
            std::vector<v8::Local<v8::Value>> v8Args;
            for (const auto& arg : event.args) {
                v8Args.push_back(v8::String::NewFromUtf8(cb.isolate, arg.c_str()).ToLocalChecked());
            }
            
            v8::TryCatch try_catch(cb.isolate);
            fn->Call(context, context->Global(), 
                static_cast<int>(v8Args.size()), 
                v8Args.empty() ? nullptr : v8Args.data());
            
            if (try_catch.HasCaught()) {
                // Log error but continue
            }
        }
    }
}

// Queue an event for later processing
static void QueueEvent(const std::string& eventName, const std::vector<std::string>& args = {}) {
    std::lock_guard<std::mutex> lock(g_eventMutex);
    
    // Check if anyone is subscribed
    auto it = g_eventCallbacks.find(eventName);
    if (it == g_eventCallbacks.end() || it->second.empty()) return;
    
    g_pendingEvents.push({eventName, args});
}

// Direct invoke - for when we're on the right thread with context
static void InvokeCallbacksDirect(const std::string& eventName, const std::vector<std::string>& args = {}) {
    std::lock_guard<std::mutex> lock(g_eventMutex);
    
    auto it = g_eventCallbacks.find(eventName);
    if (it == g_eventCallbacks.end()) return;
    
    for (auto& cb : it->second) {
        if (!cb.isolate || cb.callback.IsEmpty() || cb.context.IsEmpty()) continue;
        
        v8::Locker locker(cb.isolate);
        v8::Isolate::Scope isolate_scope(cb.isolate);
        v8::HandleScope handle_scope(cb.isolate);
        v8::Local<v8::Context> context = cb.context.Get(cb.isolate);
        v8::Context::Scope context_scope(context);
        
        v8::Local<v8::Function> fn = cb.callback.Get(cb.isolate);
        
        std::vector<v8::Local<v8::Value>> v8Args;
        for (const auto& arg : args) {
            v8Args.push_back(v8::String::NewFromUtf8(cb.isolate, arg.c_str()).ToLocalChecked());
        }
        
        v8::TryCatch try_catch(cb.isolate);
        fn->Call(context, context->Global(), 
            static_cast<int>(v8Args.size()), 
            v8Args.empty() ? nullptr : v8Args.data());
    }
}

// Frontend event callback - called from OBS main thread
static void OnFrontendEvent(enum obs_frontend_event event, void* /*data*/) {
    switch (event) {
        case OBS_FRONTEND_EVENT_SCENE_CHANGED:
            {
                obs_source_t* scene = obs_frontend_get_current_scene();
                if (scene) {
                    const char* name = obs_source_get_name(scene);
                    InvokeCallbacksDirect("sceneChanged", {name ? name : ""});
                    obs_source_release(scene);
                }
            }
            break;
            
        case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
            InvokeCallbacksDirect("sceneListChanged");
            break;
            
        case OBS_FRONTEND_EVENT_STREAMING_STARTING:
            InvokeCallbacksDirect("streamingStarting");
            break;
            
        case OBS_FRONTEND_EVENT_STREAMING_STARTED:
            InvokeCallbacksDirect("streamingStarted");
            break;
            
        case OBS_FRONTEND_EVENT_STREAMING_STOPPING:
            InvokeCallbacksDirect("streamingStopping");
            break;
            
        case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
            InvokeCallbacksDirect("streamingStopped");
            break;
            
        case OBS_FRONTEND_EVENT_RECORDING_STARTING:
            InvokeCallbacksDirect("recordingStarting");
            break;
            
        case OBS_FRONTEND_EVENT_RECORDING_STARTED:
            InvokeCallbacksDirect("recordingStarted");
            break;
            
        case OBS_FRONTEND_EVENT_RECORDING_STOPPING:
            InvokeCallbacksDirect("recordingStopping");
            break;
            
        case OBS_FRONTEND_EVENT_RECORDING_STOPPED:
            InvokeCallbacksDirect("recordingStopped");
            break;
            
        case OBS_FRONTEND_EVENT_RECORDING_PAUSED:
            InvokeCallbacksDirect("recordingPaused");
            break;
            
        case OBS_FRONTEND_EVENT_RECORDING_UNPAUSED:
            InvokeCallbacksDirect("recordingUnpaused");
            break;
            
        case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTED:
            InvokeCallbacksDirect("replayBufferStarted");
            break;
            
        case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STOPPED:
            InvokeCallbacksDirect("replayBufferStopped");
            break;
            
        case OBS_FRONTEND_EVENT_REPLAY_BUFFER_SAVED:
            InvokeCallbacksDirect("replayBufferSaved");
            break;
            
        case OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED:
            InvokeCallbacksDirect("studioModeEnabled");
            break;
            
        case OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED:
            InvokeCallbacksDirect("studioModeDisabled");
            break;
            
        case OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED:
            {
                obs_source_t* scene = obs_frontend_get_current_preview_scene();
                if (scene) {
                    const char* name = obs_source_get_name(scene);
                    InvokeCallbacksDirect("previewSceneChanged", {name ? name : ""});
                    obs_source_release(scene);
                }
            }
            break;
            
        case OBS_FRONTEND_EVENT_VIRTUALCAM_STARTED:
            InvokeCallbacksDirect("virtualCamStarted");
            break;
            
        case OBS_FRONTEND_EVENT_VIRTUALCAM_STOPPED:
            InvokeCallbacksDirect("virtualCamStopped");
            break;
            
        case OBS_FRONTEND_EVENT_EXIT:
            InvokeCallbacksDirect("exit");
            break;
            
        default:
            break;
    }
}

static void EnsureFrontendCallbackRegistered() {
    if (!g_frontendCallbackRegistered) {
        obs_frontend_add_event_callback(OnFrontendEvent, nullptr);
        g_frontendCallbackRegistered = true;
    }
}

// obs.events.on(eventName, callback) - Subscribe to an event
static void EventsOn(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    if (args.Length() < 2 || !args[0]->IsString() || !args[1]->IsFunction()) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8(isolate, "Expected (eventName, callback)").ToLocalChecked()));
        return;
    }
    
    v8::String::Utf8Value eventName(isolate, args[0]);
    v8::Local<v8::Function> callback = args[1].As<v8::Function>();
    
    EnsureFrontendCallbackRegistered();
    
    std::lock_guard<std::mutex> lock(g_eventMutex);
    
    EventCallback ec;
    ec.isolate = isolate;
    ec.callback.Reset(isolate, callback);
    ec.context.Reset(isolate, context);  // Store the context!
    
    g_eventCallbacks[*eventName].push_back(std::move(ec));
    
    args.GetReturnValue().Set(true);
}

// obs.events.off(eventName) - Unsubscribe all callbacks for an event
static void EventsOff(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    
    if (args.Length() < 1 || !args[0]->IsString()) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8(isolate, "Expected eventName").ToLocalChecked()));
        return;
    }
    
    v8::String::Utf8Value eventName(isolate, args[0]);
    
    std::lock_guard<std::mutex> lock(g_eventMutex);
    
    auto it = g_eventCallbacks.find(*eventName);
    if (it != g_eventCallbacks.end()) {
        for (auto& cb : it->second) {
            cb.callback.Reset();
            cb.context.Reset();
        }
        g_eventCallbacks.erase(it);
    }
    
    args.GetReturnValue().Set(true);
}

// obs.events.list() - List available event names
static void EventsList(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    const char* events[] = {
        "sceneChanged",
        "sceneListChanged",
        "previewSceneChanged",
        "streamingStarting",
        "streamingStarted",
        "streamingStopping",
        "streamingStopped",
        "recordingStarting",
        "recordingStarted",
        "recordingStopping",
        "recordingStopped",
        "recordingPaused",
        "recordingUnpaused",
        "replayBufferStarted",
        "replayBufferStopped",
        "replayBufferSaved",
        "studioModeEnabled",
        "studioModeDisabled",
        "virtualCamStarted",
        "virtualCamStopped",
        "exit"
    };
    
    int count = sizeof(events) / sizeof(events[0]);
    v8::Local<v8::Array> result = v8::Array::New(isolate, count);
    
    for (int i = 0; i < count; i++) {
        result->Set(context, i,
            v8::String::NewFromUtf8(isolate, events[i]).ToLocalChecked()
        ).Check();
    }
    
    args.GetReturnValue().Set(result);
}

// obs.events.process() - Manually process pending events (for testing)
static void EventsProcess(const v8::FunctionCallbackInfo<v8::Value>& args) {
    ProcessPendingEvents();
    args.GetReturnValue().Set(true);
}

void SetupEventBindings(v8::Isolate* isolate, v8::Local<v8::Object> obs) {
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    v8::Local<v8::Object> events = v8::Object::New(isolate);
    
    auto setFunc = [&](const char* name, v8::FunctionCallback cb) {
        v8::Local<v8::Function> fn = v8::Function::New(context, cb).ToLocalChecked();
        events->Set(context, 
            v8::String::NewFromUtf8(isolate, name).ToLocalChecked(),
            fn
        ).Check();
    };
    
    setFunc("on", EventsOn);
    setFunc("off", EventsOff);
    setFunc("list", EventsList);
    setFunc("process", EventsProcess);
    
    obs->Set(context,
        v8::String::NewFromUtf8(isolate, "events").ToLocalChecked(),
        events
    ).Check();
}

void CleanupEventBindings() {
    std::lock_guard<std::mutex> lock(g_eventMutex);
    
    for (auto& pair : g_eventCallbacks) {
        for (auto& cb : pair.second) {
            cb.callback.Reset();
            cb.context.Reset();
        }
    }
    g_eventCallbacks.clear();
    
    // Clear pending events
    while (!g_pendingEvents.empty()) {
        g_pendingEvents.pop();
    }
    
    if (g_frontendCallbackRegistered) {
        obs_frontend_remove_event_callback(OnFrontendEvent, nullptr);
        g_frontendCallbackRegistered = false;
    }
}

} // namespace obs_bindings
