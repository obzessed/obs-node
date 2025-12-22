/**
 * obs-scenes.cpp - OBS scene bindings
 * 
 * Exposes obs.scenes API to JavaScript.
 */

#include "obs-bindings.h"
#include <obs.h>
#include <string>
#include <vector>

namespace obs_bindings {

// obs.scenes.list() - Returns array of scene names
static void ScenesList(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    std::vector<std::string> names;
    
    auto enum_proc = [](void* param, obs_source_t* source) -> bool {
        if (obs_source_get_type(source) != OBS_SOURCE_TYPE_SCENE) {
            return true;
        }
        auto* names = static_cast<std::vector<std::string>*>(param);
        const char* name = obs_source_get_name(source);
        if (name) {
            names->push_back(name);
        }
        return true;
    };
    
    obs_enum_scenes(enum_proc, &names);
    
    v8::Local<v8::Array> result = v8::Array::New(isolate, static_cast<int>(names.size()));
    for (size_t i = 0; i < names.size(); i++) {
        v8::Local<v8::String> str = v8::String::NewFromUtf8(isolate, names[i].c_str()).ToLocalChecked();
        result->Set(context, static_cast<uint32_t>(i), str).Check();
    }
    
    args.GetReturnValue().Set(result);
}

// obs.scenes.get(name) - Returns scene info or null
static void ScenesGet(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    if (args.Length() < 1 || !args[0]->IsString()) {
        args.GetReturnValue().SetNull();
        return;
    }
    
    v8::String::Utf8Value name(isolate, args[0]);
    obs_source_t* source = obs_get_source_by_name(*name);
    
    if (!source || obs_source_get_type(source) != OBS_SOURCE_TYPE_SCENE) {
        if (source) obs_source_release(source);
        args.GetReturnValue().SetNull();
        return;
    }
    
    obs_scene_t* scene = obs_scene_from_source(source);
    
    // Get scene items
    std::vector<std::string> items;
    auto item_proc = [](obs_scene_t*, obs_sceneitem_t* item, void* param) -> bool {
        auto* items = static_cast<std::vector<std::string>*>(param);
        obs_source_t* src = obs_sceneitem_get_source(item);
        const char* name = obs_source_get_name(src);
        if (name) items->push_back(name);
        return true;
    };
    obs_scene_enum_items(scene, item_proc, &items);
    
    // Build result object
    v8::Local<v8::Object> obj = v8::Object::New(isolate);
    
    obj->Set(context, 
        v8::String::NewFromUtf8(isolate, "name").ToLocalChecked(),
        v8::String::NewFromUtf8(isolate, *name).ToLocalChecked()
    ).Check();
    
    const char* uuid = obs_source_get_uuid(source);
    if (uuid) {
        obj->Set(context, 
            v8::String::NewFromUtf8(isolate, "uuid").ToLocalChecked(),
            v8::String::NewFromUtf8(isolate, uuid).ToLocalChecked()
        ).Check();
    }
    
    v8::Local<v8::Array> itemsArray = v8::Array::New(isolate, static_cast<int>(items.size()));
    for (size_t i = 0; i < items.size(); i++) {
        itemsArray->Set(context, static_cast<uint32_t>(i),
            v8::String::NewFromUtf8(isolate, items[i].c_str()).ToLocalChecked()
        ).Check();
    }
    obj->Set(context,
        v8::String::NewFromUtf8(isolate, "items").ToLocalChecked(),
        itemsArray
    ).Check();
    
    obs_source_release(source);
    args.GetReturnValue().Set(obj);
}

// obs.scenes.create(name) - Create a new scene
static void ScenesCreate(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    
    if (args.Length() < 1 || !args[0]->IsString()) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    v8::String::Utf8Value name(isolate, args[0]);
    
    // Check if scene already exists
    obs_source_t* existing = obs_get_source_by_name(*name);
    if (existing) {
        obs_source_release(existing);
        args.GetReturnValue().Set(false);
        return;
    }
    
    obs_scene_t* scene = obs_scene_create(*name);
    if (scene) {
        obs_scene_release(scene);
        args.GetReturnValue().Set(true);
    } else {
        args.GetReturnValue().Set(false);
    }
}

void SetupSceneBindings(v8::Isolate* isolate, v8::Local<v8::Object> obs) {
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    v8::Local<v8::Object> scenes = v8::Object::New(isolate);
    
    auto setFunc = [&](const char* name, v8::FunctionCallback cb) {
        v8::Local<v8::Function> fn = v8::Function::New(context, cb).ToLocalChecked();
        scenes->Set(context, 
            v8::String::NewFromUtf8(isolate, name).ToLocalChecked(),
            fn
        ).Check();
    };
    
    setFunc("list", ScenesList);
    setFunc("get", ScenesGet);
    setFunc("create", ScenesCreate);
    
    obs->Set(context,
        v8::String::NewFromUtf8(isolate, "scenes").ToLocalChecked(),
        scenes
    ).Check();
}

} // namespace obs_bindings
