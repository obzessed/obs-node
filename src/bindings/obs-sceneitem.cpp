/**
 * obs-sceneitem.cpp - OBS scene item bindings
 * 
 * Exposes obs.sceneItems API for manipulating items within scenes.
 */

#include "obs-bindings.h"
#include <obs.h>

namespace obs_bindings {

// Helper to get scene item by scene name and source name
static obs_sceneitem_t* GetSceneItem(const char* sceneName, const char* sourceName) {
    obs_source_t* sceneSource = obs_get_source_by_name(sceneName);
    if (!sceneSource) return nullptr;
    
    obs_scene_t* scene = obs_scene_from_source(sceneSource);
    if (!scene) {
        obs_source_release(sceneSource);
        return nullptr;
    }
    
    obs_sceneitem_t* item = obs_scene_find_source(scene, sourceName);
    obs_source_release(sceneSource);
    
    return item;  // Note: sceneitem doesn't need release if not addref'd
}

// obs.sceneItems.list(sceneName) - List all items in a scene
static void SceneItemsList(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    if (args.Length() < 1 || !args[0]->IsString()) {
        args.GetReturnValue().Set(v8::Array::New(isolate, 0));
        return;
    }
    
    v8::String::Utf8Value sceneName(isolate, args[0]);
    
    obs_source_t* sceneSource = obs_get_source_by_name(*sceneName);
    if (!sceneSource) {
        args.GetReturnValue().Set(v8::Array::New(isolate, 0));
        return;
    }
    
    obs_scene_t* scene = obs_scene_from_source(sceneSource);
    if (!scene) {
        obs_source_release(sceneSource);
        args.GetReturnValue().Set(v8::Array::New(isolate, 0));
        return;
    }
    
    struct EnumData {
        v8::Isolate* isolate;
        v8::Local<v8::Context> context;
        v8::Local<v8::Array> result;
        uint32_t index = 0;
    };
    
    v8::Local<v8::Array> result = v8::Array::New(isolate);
    
    EnumData data{isolate, context, result, 0};
    
    obs_scene_enum_items(scene, [](obs_scene_t*, obs_sceneitem_t* item, void* param) -> bool {
        auto* data = static_cast<EnumData*>(param);
        obs_source_t* source = obs_sceneitem_get_source(item);
        const char* name = obs_source_get_name(source);
        
        v8::Local<v8::Object> obj = v8::Object::New(data->isolate);
        
        obj->Set(data->context,
            v8::String::NewFromUtf8(data->isolate, "name").ToLocalChecked(),
            v8::String::NewFromUtf8(data->isolate, name ? name : "").ToLocalChecked()
        ).Check();
        
        obj->Set(data->context,
            v8::String::NewFromUtf8(data->isolate, "id").ToLocalChecked(),
            v8::Integer::New(data->isolate, static_cast<int32_t>(obs_sceneitem_get_id(item)))
        ).Check();
        
        obj->Set(data->context,
            v8::String::NewFromUtf8(data->isolate, "visible").ToLocalChecked(),
            v8::Boolean::New(data->isolate, obs_sceneitem_visible(item))
        ).Check();
        
        obj->Set(data->context,
            v8::String::NewFromUtf8(data->isolate, "locked").ToLocalChecked(),
            v8::Boolean::New(data->isolate, obs_sceneitem_locked(item))
        ).Check();
        
        data->result->Set(data->context, data->index++, obj).Check();
        return true;
    }, &data);
    
    obs_source_release(sceneSource);
    args.GetReturnValue().Set(result);
}

// obs.sceneItems.setVisible(sceneName, sourceName, visible) - Set visibility
static void SceneItemsSetVisible(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    
    if (args.Length() < 3 || !args[0]->IsString() || !args[1]->IsString() || !args[2]->IsBoolean()) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    v8::String::Utf8Value sceneName(isolate, args[0]);
    v8::String::Utf8Value sourceName(isolate, args[1]);
    bool visible = args[2]->BooleanValue(isolate);
    
    obs_sceneitem_t* item = GetSceneItem(*sceneName, *sourceName);
    if (!item) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    obs_sceneitem_set_visible(item, visible);
    args.GetReturnValue().Set(true);
}

// obs.sceneItems.isVisible(sceneName, sourceName) - Get visibility
static void SceneItemsIsVisible(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    
    if (args.Length() < 2 || !args[0]->IsString() || !args[1]->IsString()) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    v8::String::Utf8Value sceneName(isolate, args[0]);
    v8::String::Utf8Value sourceName(isolate, args[1]);
    
    obs_sceneitem_t* item = GetSceneItem(*sceneName, *sourceName);
    if (!item) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    args.GetReturnValue().Set(obs_sceneitem_visible(item));
}

// obs.sceneItems.setLocked(sceneName, sourceName, locked) - Set locked state
static void SceneItemsSetLocked(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    
    if (args.Length() < 3 || !args[0]->IsString() || !args[1]->IsString() || !args[2]->IsBoolean()) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    v8::String::Utf8Value sceneName(isolate, args[0]);
    v8::String::Utf8Value sourceName(isolate, args[1]);
    bool locked = args[2]->BooleanValue(isolate);
    
    obs_sceneitem_t* item = GetSceneItem(*sceneName, *sourceName);
    if (!item) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    obs_sceneitem_set_locked(item, locked);
    args.GetReturnValue().Set(true);
}

// obs.sceneItems.getTransform(sceneName, sourceName) - Get transform info
static void SceneItemsGetTransform(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    if (args.Length() < 2 || !args[0]->IsString() || !args[1]->IsString()) {
        args.GetReturnValue().SetNull();
        return;
    }
    
    v8::String::Utf8Value sceneName(isolate, args[0]);
    v8::String::Utf8Value sourceName(isolate, args[1]);
    
    obs_sceneitem_t* item = GetSceneItem(*sceneName, *sourceName);
    if (!item) {
        args.GetReturnValue().SetNull();
        return;
    }
    
    // Use individual getters instead of deprecated obs_sceneitem_get_info
    vec2 pos, scale, bounds;
    obs_sceneitem_get_pos(item, &pos);
    obs_sceneitem_get_scale(item, &scale);
    obs_sceneitem_get_bounds(item, &bounds);
    float rot = obs_sceneitem_get_rot(item);
    
    v8::Local<v8::Object> obj = v8::Object::New(isolate);
    
    auto setNum = [&](const char* key, double val) {
        obj->Set(context, 
            v8::String::NewFromUtf8(isolate, key).ToLocalChecked(),
            v8::Number::New(isolate, val)
        ).Check();
    };
    
    setNum("posX", pos.x);
    setNum("posY", pos.y);
    setNum("rotation", rot);
    setNum("scaleX", scale.x);
    setNum("scaleY", scale.y);
    setNum("boundsX", bounds.x);
    setNum("boundsY", bounds.y);
    
    args.GetReturnValue().Set(obj);
}

// obs.sceneItems.setPosition(sceneName, sourceName, x, y) - Set position
static void SceneItemsSetPosition(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    if (args.Length() < 4 || !args[0]->IsString() || !args[1]->IsString() || 
        !args[2]->IsNumber() || !args[3]->IsNumber()) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    v8::String::Utf8Value sceneName(isolate, args[0]);
    v8::String::Utf8Value sourceName(isolate, args[1]);
    float x = static_cast<float>(args[2]->NumberValue(context).FromJust());
    float y = static_cast<float>(args[3]->NumberValue(context).FromJust());
    
    obs_sceneitem_t* item = GetSceneItem(*sceneName, *sourceName);
    if (!item) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    vec2 pos = {x, y};
    obs_sceneitem_set_pos(item, &pos);
    args.GetReturnValue().Set(true);
}

// obs.sceneItems.setScale(sceneName, sourceName, scaleX, scaleY) - Set scale
static void SceneItemsSetScale(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    if (args.Length() < 4 || !args[0]->IsString() || !args[1]->IsString() || 
        !args[2]->IsNumber() || !args[3]->IsNumber()) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    v8::String::Utf8Value sceneName(isolate, args[0]);
    v8::String::Utf8Value sourceName(isolate, args[1]);
    float scaleX = static_cast<float>(args[2]->NumberValue(context).FromJust());
    float scaleY = static_cast<float>(args[3]->NumberValue(context).FromJust());
    
    obs_sceneitem_t* item = GetSceneItem(*sceneName, *sourceName);
    if (!item) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    vec2 scale = {scaleX, scaleY};
    obs_sceneitem_set_scale(item, &scale);
    args.GetReturnValue().Set(true);
}

// obs.sceneItems.setRotation(sceneName, sourceName, rotation) - Set rotation
static void SceneItemsSetRotation(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    if (args.Length() < 3 || !args[0]->IsString() || !args[1]->IsString() || !args[2]->IsNumber()) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    v8::String::Utf8Value sceneName(isolate, args[0]);
    v8::String::Utf8Value sourceName(isolate, args[1]);
    float rotation = static_cast<float>(args[2]->NumberValue(context).FromJust());
    
    obs_sceneitem_t* item = GetSceneItem(*sceneName, *sourceName);
    if (!item) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    obs_sceneitem_set_rot(item, rotation);
    args.GetReturnValue().Set(true);
}

// obs.sceneItems.remove(sceneName, sourceName) - Remove item from scene
static void SceneItemsRemove(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    
    if (args.Length() < 2 || !args[0]->IsString() || !args[1]->IsString()) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    v8::String::Utf8Value sceneName(isolate, args[0]);
    v8::String::Utf8Value sourceName(isolate, args[1]);
    
    obs_sceneitem_t* item = GetSceneItem(*sceneName, *sourceName);
    if (!item) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    obs_sceneitem_remove(item);
    args.GetReturnValue().Set(true);
}

// obs.sceneItems.add(sceneName, sourceName) - Add source to scene
static void SceneItemsAdd(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    
    if (args.Length() < 2 || !args[0]->IsString() || !args[1]->IsString()) {
        args.GetReturnValue().Set(v8::Integer::New(isolate, -1));
        return;
    }
    
    v8::String::Utf8Value sceneName(isolate, args[0]);
    v8::String::Utf8Value sourceName(isolate, args[1]);
    
    obs_source_t* sceneSource = obs_get_source_by_name(*sceneName);
    if (!sceneSource) {
        args.GetReturnValue().Set(v8::Integer::New(isolate, -1));
        return;
    }
    
    obs_scene_t* scene = obs_scene_from_source(sceneSource);
    if (!scene) {
        obs_source_release(sceneSource);
        args.GetReturnValue().Set(v8::Integer::New(isolate, -1));
        return;
    }
    
    obs_source_t* source = obs_get_source_by_name(*sourceName);
    if (!source) {
        obs_source_release(sceneSource);
        args.GetReturnValue().Set(v8::Integer::New(isolate, -1));
        return;
    }
    
    obs_sceneitem_t* item = obs_scene_add(scene, source);
    int64_t id = item ? obs_sceneitem_get_id(item) : -1;
    
    obs_source_release(source);
    obs_source_release(sceneSource);
    
    args.GetReturnValue().Set(v8::Integer::New(isolate, (int32_t)id));
}



// obs.sceneItems.setOrder(sceneName, sourceName, order) - Set order (top, bottom, up, down)
// order: 0=Top, 1=Bottom, 2=Up, 3=Down
static void SceneItemsSetOrder(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    
    if (args.Length() < 3 || !args[0]->IsString() || !args[1]->IsString() || !args[2]->IsNumber()) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    v8::String::Utf8Value sceneName(isolate, args[0]);
    v8::String::Utf8Value sourceName(isolate, args[1]);
    int order = args[2]->Int32Value(isolate->GetCurrentContext()).FromJust();
    
    obs_sceneitem_t* item = GetSceneItem(*sceneName, *sourceName);
    if (!item) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    obs_sceneitem_set_order(item, (obs_order_movement)order);
    args.GetReturnValue().Set(true);
}

// obs.sceneItems.setAlignment(sceneName, sourceName, alignment) - Set alignment
static void SceneItemsSetAlignment(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    
    if (args.Length() < 3 || !args[0]->IsString() || !args[1]->IsString() || !args[2]->IsNumber()) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    v8::String::Utf8Value sceneName(isolate, args[0]);
    v8::String::Utf8Value sourceName(isolate, args[1]);
    int alignment = args[2]->Int32Value(isolate->GetCurrentContext()).FromJust();
    
    obs_sceneitem_t* item = GetSceneItem(*sceneName, *sourceName);
    if (!item) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    obs_sceneitem_set_alignment(item, alignment);
    args.GetReturnValue().Set(true);
}

// obs.sceneItems.setBounds(sceneName, sourceName, x, y, alignment, type) 
static void SceneItemsSetBounds(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    if (args.Length() < 4 || !args[0]->IsString() || !args[1]->IsString() || 
        !args[2]->IsNumber() || !args[3]->IsNumber()) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    v8::String::Utf8Value sceneName(isolate, args[0]);
    v8::String::Utf8Value sourceName(isolate, args[1]);
    float x = static_cast<float>(args[2]->NumberValue(context).FromJust());
    float y = static_cast<float>(args[3]->NumberValue(context).FromJust());
    
    uint32_t alignment = 0;
    uint32_t type = 0; // OBS_BOUNDS_NONE
    
    if (args.Length() > 4 && args[4]->IsNumber()) alignment = args[4]->Uint32Value(context).FromJust();
    if (args.Length() > 5 && args[5]->IsNumber()) type = args[5]->Uint32Value(context).FromJust();
    
    obs_sceneitem_t* item = GetSceneItem(*sceneName, *sourceName);
    if (!item) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    vec2 bounds = {x, y};
    obs_sceneitem_set_bounds(item, &bounds);
    obs_sceneitem_set_bounds_alignment(item, alignment);
    obs_sceneitem_set_bounds_type(item, (obs_bounds_type)type);
    
    args.GetReturnValue().Set(true);
}

void SetupSceneItemBindings(v8::Isolate* isolate, v8::Local<v8::Object> obs) {
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    v8::Local<v8::Object> sceneItems = v8::Object::New(isolate);
    
    auto setFunc = [&](const char* name, v8::FunctionCallback cb) {
        v8::Local<v8::Function> fn = v8::Function::New(context, cb).ToLocalChecked();
        sceneItems->Set(context, 
            v8::String::NewFromUtf8(isolate, name).ToLocalChecked(),
            fn
        ).Check();
    };
    
    setFunc("list", SceneItemsList);
    setFunc("setVisible", SceneItemsSetVisible);
    setFunc("isVisible", SceneItemsIsVisible);
    setFunc("setLocked", SceneItemsSetLocked);
    setFunc("getTransform", SceneItemsGetTransform);
    setFunc("setPosition", SceneItemsSetPosition);
    setFunc("setScale", SceneItemsSetScale);
    setFunc("setRotation", SceneItemsSetRotation);
    setFunc("remove", SceneItemsRemove);
    setFunc("add", SceneItemsAdd);
    setFunc("setOrder", SceneItemsSetOrder);
    setFunc("setAlignment", SceneItemsSetAlignment);
    setFunc("setBounds", SceneItemsSetBounds);
    
    obs->Set(context,
        v8::String::NewFromUtf8(isolate, "sceneItems").ToLocalChecked(),
        sceneItems
    ).Check();
}

} // namespace obs_bindings
