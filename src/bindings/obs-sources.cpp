/**
 * obs-sources.cpp - OBS source bindings
 * 
 * Exposes obs.sources API to JavaScript.
 */

#include "obs-bindings.h"
#include <obs.h>
#include <string>
#include <vector>

namespace obs_bindings {

// obs.sources.list() - Returns array of source names
static void SourcesList(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    std::vector<std::string> names;
    
    // Enumerate all sources
    auto enum_proc = [](void* param, obs_source_t* source) -> bool {
        auto* names = static_cast<std::vector<std::string>*>(param);
        const char* name = obs_source_get_name(source);
        if (name) {
            names->push_back(name);
        }
        return true;
    };
    
    obs_enum_sources(enum_proc, &names);
    
    // Convert to JS array
    v8::Local<v8::Array> result = v8::Array::New(isolate, static_cast<int>(names.size()));
    for (size_t i = 0; i < names.size(); i++) {
        v8::Local<v8::String> str = v8::String::NewFromUtf8(isolate, names[i].c_str()).ToLocalChecked();
        result->Set(context, static_cast<uint32_t>(i), str).Check();
    }
    
    args.GetReturnValue().Set(result);
}

// obs.sources.get(name) - Returns source info object or null
static void SourcesGet(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    if (args.Length() < 1 || !args[0]->IsString()) {
        args.GetReturnValue().SetNull();
        return;
    }
    
    v8::String::Utf8Value name(isolate, args[0]);
    obs_source_t* source = obs_get_source_by_name(*name);
    
    if (!source) {
        args.GetReturnValue().SetNull();
        return;
    }
    
    // Build source info object
    v8::Local<v8::Object> obj = v8::Object::New(isolate);
    
    auto set = [&](const char* key, v8::Local<v8::Value> val) {
        obj->Set(context, v8::String::NewFromUtf8(isolate, key).ToLocalChecked(), val).Check();
    };
    
    set("name", v8::String::NewFromUtf8(isolate, obs_source_get_name(source)).ToLocalChecked());
    set("id", v8::String::NewFromUtf8(isolate, obs_source_get_id(source)).ToLocalChecked());
    set("type", v8::Integer::New(isolate, static_cast<int>(obs_source_get_type(source))));
    set("width", v8::Integer::NewFromUnsigned(isolate, obs_source_get_width(source)));
    set("height", v8::Integer::NewFromUnsigned(isolate, obs_source_get_height(source)));
    set("enabled", v8::Boolean::New(isolate, obs_source_enabled(source)));
    set("active", v8::Boolean::New(isolate, obs_source_active(source)));
    set("showing", v8::Boolean::New(isolate, obs_source_showing(source)));
    set("muted", v8::Boolean::New(isolate, obs_source_muted(source)));
    
    const char* uuid = obs_source_get_uuid(source);
    if (uuid) {
        set("uuid", v8::String::NewFromUtf8(isolate, uuid).ToLocalChecked());
    }
    
    obs_source_release(source);
    
    args.GetReturnValue().Set(obj);
}

// obs.sources.getTypes() - Returns array of available source type IDs
static void SourcesGetTypes(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    std::vector<std::string> types;
    
    // Get input sources
    size_t idx = 0;
    const char* id;
    while (obs_enum_input_types(idx++, &id)) {
        if (id) types.push_back(id);
    }
    
    v8::Local<v8::Array> result = v8::Array::New(isolate, static_cast<int>(types.size()));
    for (size_t i = 0; i < types.size(); i++) {
        v8::Local<v8::String> str = v8::String::NewFromUtf8(isolate, types[i].c_str()).ToLocalChecked();
        result->Set(context, static_cast<uint32_t>(i), str).Check();
    }
    
    args.GetReturnValue().Set(result);
}

// obs.sources.setEnabled(name, enabled) - Enable/disable a source
static void SourcesSetEnabled(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    
    if (args.Length() < 2 || !args[0]->IsString() || !args[1]->IsBoolean()) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    v8::String::Utf8Value name(isolate, args[0]);
    bool enabled = args[1]->BooleanValue(isolate);
    
    obs_source_t* source = obs_get_source_by_name(*name);
    if (!source) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    obs_source_set_enabled(source, enabled);
    obs_source_release(source);
    
    args.GetReturnValue().Set(true);
}

// obs.sources.setMuted(name, muted) - Mute/unmute a source
static void SourcesSetMuted(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    
    if (args.Length() < 2 || !args[0]->IsString() || !args[1]->IsBoolean()) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    v8::String::Utf8Value name(isolate, args[0]);
    bool muted = args[1]->BooleanValue(isolate);
    
    obs_source_t* source = obs_get_source_by_name(*name);
    if (!source) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    obs_source_set_muted(source, muted);
    obs_source_release(source);
    
    args.GetReturnValue().Set(true);
}

// obs.sources.setName(oldName, newName)
static void SourcesSetName(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    
    if (args.Length() < 2 || !args[0]->IsString() || !args[1]->IsString()) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    v8::String::Utf8Value oldName(isolate, args[0]);
    v8::String::Utf8Value newName(isolate, args[1]);
    
    obs_source_t* source = obs_get_source_by_name(*oldName);
    if (!source) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    obs_source_set_name(source, *newName);
    obs_source_release(source);
    
    args.GetReturnValue().Set(true);
}

void SetupSourceBindings(v8::Isolate* isolate, v8::Local<v8::Object> obs) {
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    // Create obs.sources object
    v8::Local<v8::Object> sources = v8::Object::New(isolate);
    
    auto setFunc = [&](const char* name, v8::FunctionCallback cb) {
        v8::Local<v8::Function> fn = v8::Function::New(context, cb).ToLocalChecked();
        sources->Set(context, 
            v8::String::NewFromUtf8(isolate, name).ToLocalChecked(),
            fn
        ).Check();
    };
    
    setFunc("list", SourcesList);
    setFunc("get", SourcesGet);
    setFunc("getTypes", SourcesGetTypes);
    setFunc("setEnabled", SourcesSetEnabled);
    setFunc("setMuted", SourcesSetMuted);
    setFunc("setName", SourcesSetName);
    
    obs->Set(context,
        v8::String::NewFromUtf8(isolate, "sources").ToLocalChecked(),
        sources
    ).Check();
}

} // namespace obs_bindings
