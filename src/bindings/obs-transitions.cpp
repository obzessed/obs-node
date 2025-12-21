/**
 * obs-transitions.cpp - OBS transition bindings
 * 
 * Exposes obs.transitions API for managing scene transitions.
 */

#include "obs-bindings.h"
#include <obs.h>
#include <obs-frontend-api.h>

namespace obs_bindings {

// obs.transitions.list() - List available transitions
static void TransitionsList(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    obs_frontend_source_list transitions = {};
    obs_frontend_get_transitions(&transitions);
    
    v8::Local<v8::Array> result = v8::Array::New(isolate, static_cast<int>(transitions.sources.num));
    
    for (size_t i = 0; i < transitions.sources.num; i++) {
        obs_source_t* source = transitions.sources.array[i];
        const char* name = obs_source_get_name(source);
        
        result->Set(context, static_cast<uint32_t>(i),
            v8::String::NewFromUtf8(isolate, name ? name : "").ToLocalChecked()
        ).Check();
    }
    
    obs_frontend_source_list_free(&transitions);
    args.GetReturnValue().Set(result);
}

// obs.transitions.getTypes() - Get available transition type IDs
static void TransitionsGetTypes(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    v8::Local<v8::Array> result = v8::Array::New(isolate);
    uint32_t index = 0;
    
    size_t i = 0;
    const char* id;
    while (obs_enum_transition_types(i++, &id)) {
        result->Set(context, index++,
            v8::String::NewFromUtf8(isolate, id).ToLocalChecked()
        ).Check();
    }
    
    args.GetReturnValue().Set(result);
}

// obs.transitions.getCurrent() - Get current transition name
static void TransitionsGetCurrent(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    
    obs_source_t* transition = obs_frontend_get_current_transition();
    if (!transition) {
        args.GetReturnValue().SetNull();
        return;
    }
    
    const char* name = obs_source_get_name(transition);
    v8::Local<v8::String> result = v8::String::NewFromUtf8(isolate, name ? name : "").ToLocalChecked();
    obs_source_release(transition);
    
    args.GetReturnValue().Set(result);
}

// obs.transitions.setCurrent(name) - Set current transition
static void TransitionsSetCurrent(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    
    if (args.Length() < 1 || !args[0]->IsString()) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    v8::String::Utf8Value name(isolate, args[0]);
    
    // Find the transition by name
    obs_frontend_source_list transitions = {};
    obs_frontend_get_transitions(&transitions);
    
    bool found = false;
    for (size_t i = 0; i < transitions.sources.num; i++) {
        obs_source_t* source = transitions.sources.array[i];
        const char* sname = obs_source_get_name(source);
        if (sname && strcmp(sname, *name) == 0) {
            obs_frontend_set_current_transition(source);
            found = true;
            break;
        }
    }
    
    obs_frontend_source_list_free(&transitions);
    args.GetReturnValue().Set(found);
}

// obs.transitions.getDuration() - Get transition duration in ms
static void TransitionsGetDuration(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    
    int duration = obs_frontend_get_transition_duration();
    args.GetReturnValue().Set(v8::Integer::New(isolate, duration));
}

// obs.transitions.setDuration(ms) - Set transition duration in ms
static void TransitionsSetDuration(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    if (args.Length() < 1 || !args[0]->IsNumber()) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    int duration = args[0]->Int32Value(context).FromJust();
    obs_frontend_set_transition_duration(duration);
    args.GetReturnValue().Set(true);
}

void SetupTransitionBindings(v8::Isolate* isolate, v8::Local<v8::Object> obs) {
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    v8::Local<v8::Object> transitions = v8::Object::New(isolate);
    
    auto setFunc = [&](const char* name, v8::FunctionCallback cb) {
        v8::Local<v8::Function> fn = v8::Function::New(context, cb).ToLocalChecked();
        transitions->Set(context, 
            v8::String::NewFromUtf8(isolate, name).ToLocalChecked(),
            fn
        ).Check();
    };
    
    setFunc("list", TransitionsList);
    setFunc("getTypes", TransitionsGetTypes);
    setFunc("getCurrent", TransitionsGetCurrent);
    setFunc("setCurrent", TransitionsSetCurrent);
    setFunc("getDuration", TransitionsGetDuration);
    setFunc("setDuration", TransitionsSetDuration);
    
    obs->Set(context,
        v8::String::NewFromUtf8(isolate, "transitions").ToLocalChecked(),
        transitions
    ).Check();
}

} // namespace obs_bindings
