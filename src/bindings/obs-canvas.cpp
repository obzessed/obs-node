/**
 * obs-canvas.cpp - OBS canvas/video API bindings
 * 
 * Exposes obs.canvas API to JavaScript for video configuration.
 */

#include "obs-bindings.h"
#include <obs.h>

namespace obs_bindings {

// obs.canvas.getBaseResolution() - Returns {width, height}
static void GetBaseResolution(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    obs_video_info ovi;
    if (!obs_get_video_info(&ovi)) {
        args.GetReturnValue().SetNull();
        return;
    }
    
    v8::Local<v8::Object> obj = v8::Object::New(isolate);
    obj->Set(context, 
        v8::String::NewFromUtf8(isolate, "width").ToLocalChecked(),
        v8::Integer::NewFromUnsigned(isolate, ovi.base_width)
    ).Check();
    obj->Set(context,
        v8::String::NewFromUtf8(isolate, "height").ToLocalChecked(),
        v8::Integer::NewFromUnsigned(isolate, ovi.base_height)
    ).Check();
    
    args.GetReturnValue().Set(obj);
}

// obs.canvas.getOutputResolution() - Returns {width, height}
static void GetOutputResolution(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    obs_video_info ovi;
    if (!obs_get_video_info(&ovi)) {
        args.GetReturnValue().SetNull();
        return;
    }
    
    v8::Local<v8::Object> obj = v8::Object::New(isolate);
    obj->Set(context, 
        v8::String::NewFromUtf8(isolate, "width").ToLocalChecked(),
        v8::Integer::NewFromUnsigned(isolate, ovi.output_width)
    ).Check();
    obj->Set(context,
        v8::String::NewFromUtf8(isolate, "height").ToLocalChecked(),
        v8::Integer::NewFromUnsigned(isolate, ovi.output_height)
    ).Check();
    
    args.GetReturnValue().Set(obj);
}

// obs.canvas.getFps() - Returns {num, den, fps}
static void GetFps(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    obs_video_info ovi;
    if (!obs_get_video_info(&ovi)) {
        args.GetReturnValue().SetNull();
        return;
    }
    
    v8::Local<v8::Object> obj = v8::Object::New(isolate);
    obj->Set(context, 
        v8::String::NewFromUtf8(isolate, "num").ToLocalChecked(),
        v8::Integer::NewFromUnsigned(isolate, ovi.fps_num)
    ).Check();
    obj->Set(context,
        v8::String::NewFromUtf8(isolate, "den").ToLocalChecked(),
        v8::Integer::NewFromUnsigned(isolate, ovi.fps_den)
    ).Check();
    
    double fps = (ovi.fps_den > 0) ? (double)ovi.fps_num / (double)ovi.fps_den : 0;
    obj->Set(context,
        v8::String::NewFromUtf8(isolate, "fps").ToLocalChecked(),
        v8::Number::New(isolate, fps)
    ).Check();
    
    args.GetReturnValue().Set(obj);
}

// obs.canvas.getVideoInfo() - Returns full video configuration
static void GetVideoInfo(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    obs_video_info ovi;
    if (!obs_get_video_info(&ovi)) {
        args.GetReturnValue().SetNull();
        return;
    }
    
    v8::Local<v8::Object> obj = v8::Object::New(isolate);
    
    auto setUint = [&](const char* key, uint32_t val) {
        obj->Set(context, 
            v8::String::NewFromUtf8(isolate, key).ToLocalChecked(),
            v8::Integer::NewFromUnsigned(isolate, val)
        ).Check();
    };
    
    auto setNum = [&](const char* key, double val) {
        obj->Set(context, 
            v8::String::NewFromUtf8(isolate, key).ToLocalChecked(),
            v8::Number::New(isolate, val)
        ).Check();
    };
    
    setUint("baseWidth", ovi.base_width);
    setUint("baseHeight", ovi.base_height);
    setUint("outputWidth", ovi.output_width);
    setUint("outputHeight", ovi.output_height);
    setUint("fpsNum", ovi.fps_num);
    setUint("fpsDen", ovi.fps_den);
    
    double fps = (ovi.fps_den > 0) ? (double)ovi.fps_num / (double)ovi.fps_den : 0;
    setNum("fps", fps);
    
    // Color format info
    setUint("colorspace", static_cast<uint32_t>(ovi.colorspace));
    setUint("range", static_cast<uint32_t>(ovi.range));
    setUint("gpuConversion", ovi.gpu_conversion ? 1 : 0);
    setUint("scaleType", static_cast<uint32_t>(ovi.scale_type));
    
    args.GetReturnValue().Set(obj);
}

// obs.canvas.getOutputs() - List active outputs
static void GetOutputs(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    std::vector<std::string> outputs;
    
    auto enum_proc = [](void* param, obs_output_t* output) -> bool {
        auto* outputs = static_cast<std::vector<std::string>*>(param);
        const char* name = obs_output_get_name(output);
        if (name) outputs->push_back(name);
        return true;
    };
    
    obs_enum_outputs(enum_proc, &outputs);
    
    v8::Local<v8::Array> result = v8::Array::New(isolate, static_cast<int>(outputs.size()));
    for (size_t i = 0; i < outputs.size(); i++) {
        result->Set(context, static_cast<uint32_t>(i),
            v8::String::NewFromUtf8(isolate, outputs[i].c_str()).ToLocalChecked()
        ).Check();
    }
    
    args.GetReturnValue().Set(result);
}

// obs.canvas.getOutput(name) - Get output info
static void GetOutput(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    if (args.Length() < 1 || !args[0]->IsString()) {
        args.GetReturnValue().SetNull();
        return;
    }
    
    v8::String::Utf8Value name(isolate, args[0]);
    obs_output_t* output = obs_get_output_by_name(*name);
    
    if (!output) {
        args.GetReturnValue().SetNull();
        return;
    }
    
    v8::Local<v8::Object> obj = v8::Object::New(isolate);
    
    obj->Set(context,
        v8::String::NewFromUtf8(isolate, "name").ToLocalChecked(),
        v8::String::NewFromUtf8(isolate, obs_output_get_name(output)).ToLocalChecked()
    ).Check();
    
    obj->Set(context,
        v8::String::NewFromUtf8(isolate, "id").ToLocalChecked(),
        v8::String::NewFromUtf8(isolate, obs_output_get_id(output)).ToLocalChecked()
    ).Check();
    
    obj->Set(context,
        v8::String::NewFromUtf8(isolate, "width").ToLocalChecked(),
        v8::Integer::NewFromUnsigned(isolate, obs_output_get_width(output))
    ).Check();
    
    obj->Set(context,
        v8::String::NewFromUtf8(isolate, "height").ToLocalChecked(),
        v8::Integer::NewFromUnsigned(isolate, obs_output_get_height(output))
    ).Check();
    
    obj->Set(context,
        v8::String::NewFromUtf8(isolate, "active").ToLocalChecked(),
        v8::Boolean::New(isolate, obs_output_active(output))
    ).Check();
    
    obj->Set(context,
        v8::String::NewFromUtf8(isolate, "totalFrames").ToLocalChecked(),
        v8::Integer::New(isolate, obs_output_get_total_frames(output))
    ).Check();
    
    obj->Set(context,
        v8::String::NewFromUtf8(isolate, "droppedFrames").ToLocalChecked(),
        v8::Integer::New(isolate, obs_output_get_frames_dropped(output))
    ).Check();
    
    obs_output_release(output);
    args.GetReturnValue().Set(obj);
}

// ============================================================================
// OBS 31+ Multi-Canvas API (obs_canvas_t)
// ============================================================================

// Helper to build canvas info object
static v8::Local<v8::Object> BuildCanvasInfo(v8::Isolate* isolate, v8::Local<v8::Context> context, obs_canvas_t* canvas) {
    v8::Local<v8::Object> obj = v8::Object::New(isolate);
    
    auto setStr = [&](const char* key, const char* val) {
        obj->Set(context, 
            v8::String::NewFromUtf8(isolate, key).ToLocalChecked(),
            v8::String::NewFromUtf8(isolate, val ? val : "").ToLocalChecked()
        ).Check();
    };
    
    auto setUint = [&](const char* key, uint32_t val) {
        obj->Set(context, 
            v8::String::NewFromUtf8(isolate, key).ToLocalChecked(),
            v8::Integer::NewFromUnsigned(isolate, val)
        ).Check();
    };
    
    auto setBool = [&](const char* key, bool val) {
        obj->Set(context, 
            v8::String::NewFromUtf8(isolate, key).ToLocalChecked(),
            v8::Boolean::New(isolate, val)
        ).Check();
    };
    
    setStr("name", obs_canvas_get_name(canvas));
    setStr("uuid", obs_canvas_get_uuid(canvas));
    setUint("flags", obs_canvas_get_flags(canvas));
    setBool("hasVideo", obs_canvas_has_video(canvas));
    setBool("removed", obs_canvas_removed(canvas));
    
    // Get video info if available
    obs_video_info ovi;
    if (obs_canvas_get_video_info(canvas, &ovi)) {
        setUint("baseWidth", ovi.base_width);
        setUint("baseHeight", ovi.base_height);
        setUint("outputWidth", ovi.output_width);
        setUint("outputHeight", ovi.output_height);
    }
    
    return obj;
}

// obs.canvas.getMain() - Get main canvas info
static void CanvasGetMain(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    obs_canvas_t* canvas = obs_get_main_canvas();
    if (!canvas) {
        args.GetReturnValue().SetNull();
        return;
    }
    
    v8::Local<v8::Object> obj = BuildCanvasInfo(isolate, context, canvas);
    obs_canvas_release(canvas);
    
    args.GetReturnValue().Set(obj);
}

// obs.canvas.list() - List all canvases
static void CanvasList(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    std::vector<std::string> names;
    
    auto enum_proc = [](void* param, obs_canvas_t* canvas) -> bool {
        auto* names = static_cast<std::vector<std::string>*>(param);
        const char* name = obs_canvas_get_name(canvas);
        if (name) names->push_back(name);
        return true;
    };
    
    obs_enum_canvases(enum_proc, &names);
    
    v8::Local<v8::Array> result = v8::Array::New(isolate, static_cast<int>(names.size()));
    for (size_t i = 0; i < names.size(); i++) {
        result->Set(context, static_cast<uint32_t>(i),
            v8::String::NewFromUtf8(isolate, names[i].c_str()).ToLocalChecked()
        ).Check();
    }
    
    args.GetReturnValue().Set(result);
}

// obs.canvas.get(name) - Get canvas by name
static void CanvasGet(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    if (args.Length() < 1 || !args[0]->IsString()) {
        args.GetReturnValue().SetNull();
        return;
    }
    
    v8::String::Utf8Value name(isolate, args[0]);
    obs_canvas_t* canvas = obs_get_canvas_by_name(*name);
    
    if (!canvas) {
        args.GetReturnValue().SetNull();
        return;
    }
    
    v8::Local<v8::Object> obj = BuildCanvasInfo(isolate, context, canvas);
    obs_canvas_release(canvas);
    
    args.GetReturnValue().Set(obj);
}

// obs.canvas.getScenes(canvasName) - Get scenes for a canvas
static void CanvasGetScenes(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    if (args.Length() < 1 || !args[0]->IsString()) {
        args.GetReturnValue().Set(v8::Array::New(isolate, 0));
        return;
    }
    
    v8::String::Utf8Value name(isolate, args[0]);
    obs_canvas_t* canvas = obs_get_canvas_by_name(*name);
    
    if (!canvas) {
        args.GetReturnValue().Set(v8::Array::New(isolate, 0));
        return;
    }
    
    std::vector<std::string> scenes;
    
    auto enum_proc = [](void* param, obs_source_t* source) -> bool {
        auto* scenes = static_cast<std::vector<std::string>*>(param);
        const char* name = obs_source_get_name(source);
        if (name) scenes->push_back(name);
        return true;
    };
    
    obs_canvas_enum_scenes(canvas, enum_proc, &scenes);
    obs_canvas_release(canvas);
    
    v8::Local<v8::Array> result = v8::Array::New(isolate, static_cast<int>(scenes.size()));
    for (size_t i = 0; i < scenes.size(); i++) {
        result->Set(context, static_cast<uint32_t>(i),
            v8::String::NewFromUtf8(isolate, scenes[i].c_str()).ToLocalChecked()
        ).Check();
    }
    
    args.GetReturnValue().Set(result);
}

// obs.canvas.setName(canvasName, newName) - Rename a canvas
static void CanvasSetName(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    
    if (args.Length() < 2 || !args[0]->IsString() || !args[1]->IsString()) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    v8::String::Utf8Value oldName(isolate, args[0]);
    v8::String::Utf8Value newName(isolate, args[1]);
    
    obs_canvas_t* canvas = obs_get_canvas_by_name(*oldName);
    if (!canvas) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    obs_canvas_set_name(canvas, *newName);
    obs_canvas_release(canvas);
    
    args.GetReturnValue().Set(true);
}

// obs.canvas.create(name, settings?, flags?)
// settings: { baseWidth, baseHeight, outputWidth, outputHeight, fpsNum, fpsDen }
// flags: number (canvas flags)
static void CanvasCreate(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    if (args.Length() < 1 || !args[0]->IsString()) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    v8::String::Utf8Value name(isolate, args[0]);
    
    // Get current video info as default
    obs_video_info ovi;
    if (!obs_get_video_info(&ovi)) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    // Override with settings if provided
    if (args.Length() >= 2 && args[1]->IsObject()) {
        v8::Local<v8::Object> settings = args[1].As<v8::Object>();
        auto getUint = [&](const char* key, uint32_t& target) {
            v8::Local<v8::String> k = v8::String::NewFromUtf8(isolate, key).ToLocalChecked();
            if (settings->Has(context, k).FromMaybe(false)) {
                v8::Local<v8::Value> v = settings->Get(context, k).ToLocalChecked();
                if (v->IsNumber()) target = v->Uint32Value(context).FromMaybe(target);
            }
        };
        getUint("baseWidth", ovi.base_width);
        getUint("baseHeight", ovi.base_height);
        getUint("outputWidth", ovi.output_width);
        getUint("outputHeight", ovi.output_height);
        getUint("fpsNum", ovi.fps_num);
        getUint("fpsDen", ovi.fps_den);
    }
    
    uint32_t flags = 0;
    if (args.Length() >= 3 && args[2]->IsNumber()) {
        flags = args[2]->Uint32Value(context).FromMaybe(0);
    }
    
    obs_canvas_t* canvas = obs_canvas_create(*name, &ovi, flags);
    if (!canvas) {
        args.GetReturnValue().Set(false);
        return;
    }
    obs_canvas_release(canvas);
    args.GetReturnValue().Set(true);
}

// obs.canvas.createPrivate(name, settings?, flags?)
static void CanvasCreatePrivate(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    if (args.Length() < 1 || !args[0]->IsString()) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    v8::String::Utf8Value name(isolate, args[0]);
    
    obs_video_info ovi;
    if (!obs_get_video_info(&ovi)) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    if (args.Length() >= 2 && args[1]->IsObject()) {
        v8::Local<v8::Object> settings = args[1].As<v8::Object>();
        auto getUint = [&](const char* key, uint32_t& target) {
            v8::Local<v8::String> k = v8::String::NewFromUtf8(isolate, key).ToLocalChecked();
            if (settings->Has(context, k).FromMaybe(false)) {
                v8::Local<v8::Value> v = settings->Get(context, k).ToLocalChecked();
                if (v->IsNumber()) target = v->Uint32Value(context).FromMaybe(target);
            }
        };
        getUint("baseWidth", ovi.base_width);
        getUint("baseHeight", ovi.base_height);
        getUint("outputWidth", ovi.output_width);
        getUint("outputHeight", ovi.output_height);
        getUint("fpsNum", ovi.fps_num);
        getUint("fpsDen", ovi.fps_den);
    }
    
    uint32_t flags = 0;
    if (args.Length() >= 3 && args[2]->IsNumber()) {
        flags = args[2]->Uint32Value(context).FromMaybe(0);
    }
    
    obs_canvas_t* canvas = obs_canvas_create_private(*name, &ovi, flags);
    if (!canvas) {
        args.GetReturnValue().Set(false);
        return;
    }
    obs_canvas_release(canvas);
    args.GetReturnValue().Set(true);
}

// obs.canvas.remove(name)
static void CanvasRemove(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    if (args.Length() < 1 || !args[0]->IsString()) {
        args.GetReturnValue().Set(false);
        return;
    }
    v8::String::Utf8Value name(isolate, args[0]);
    obs_canvas_t* canvas = obs_get_canvas_by_name(*name);
    if (!canvas) {
        args.GetReturnValue().Set(false);
        return;
    }
    obs_canvas_remove(canvas);
    obs_canvas_release(canvas);
    args.GetReturnValue().Set(true);
}

// obs.canvas.save(canvasName) -> returns obs_data as JSON string
static void CanvasSave(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    if (args.Length() < 1 || !args[0]->IsString()) {
        args.GetReturnValue().SetNull();
        return;
    }
    v8::String::Utf8Value name(isolate, args[0]);
    
    obs_canvas_t* canvas = obs_get_canvas_by_name(*name);
    if (!canvas) {
        args.GetReturnValue().SetNull();
        return;
    }
    
    obs_data_t* data = obs_save_canvas(canvas);
    obs_canvas_release(canvas);
    
    if (!data) {
        args.GetReturnValue().SetNull();
        return;
    }

    if (const char *json = obs_data_get_json(data)) {
        args.GetReturnValue().Set(v8::String::NewFromUtf8(isolate, json).ToLocalChecked());
    } else {
        args.GetReturnValue().SetNull();
    }
    obs_data_release(data);
}

// obs.canvas.load(jsonData) -> creates and returns canvas name (or null)
// obs_load_canvas(obs_data_t*) returns obs_canvas_t*
static void CanvasLoad(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    if (args.Length() < 1 || !args[0]->IsString()) {
        args.GetReturnValue().SetNull();
        return;
    }
    v8::String::Utf8Value jsonData(isolate, args[0]);
    
    obs_data_t* data = obs_data_create_from_json(*jsonData);
    if (!data) {
        args.GetReturnValue().SetNull();
        return;
    }
    
    obs_canvas_t* canvas = obs_load_canvas(data);
    obs_data_release(data);
    
    if (!canvas) {
        args.GetReturnValue().SetNull();
        return;
    }
    
    const char* name = obs_canvas_get_name(canvas);
    if (name) {
        args.GetReturnValue().Set(v8::String::NewFromUtf8(isolate, name).ToLocalChecked());
    } else {
        args.GetReturnValue().SetNull();
    }
    obs_canvas_release(canvas);
}

// obs.canvas.addScene(canvasName, sceneName) - Creates a NEW scene on the canvas
static void CanvasAddScene(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    if (args.Length() < 2 || !args[0]->IsString() || !args[1]->IsString()) {
        args.GetReturnValue().Set(false);
        return;
    }
    v8::String::Utf8Value canvasName(isolate, args[0]);
    v8::String::Utf8Value sceneName(isolate, args[1]);
    
    obs_canvas_t* canvas = obs_get_canvas_by_name(*canvasName);
    if (!canvas) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    obs_scene_t* scene = obs_canvas_scene_create(canvas, *sceneName);
    obs_canvas_release(canvas);
    
    args.GetReturnValue().Set(scene != nullptr);
}

// obs.canvas.removeScene(sceneName) - obs_canvas_scene_remove takes only scene
static void CanvasRemoveScene(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    if (args.Length() < 1 || !args[0]->IsString()) {
        args.GetReturnValue().Set(false);
        return;
    }
    v8::String::Utf8Value sceneName(isolate, args[0]);
    
    obs_source_t* sceneSource = obs_get_source_by_name(*sceneName);
    if (!sceneSource) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    obs_scene_t* scene = obs_scene_from_source(sceneSource);
    if (scene) {
        obs_canvas_scene_remove(scene);
    }
    
    obs_source_release(sceneSource);
    args.GetReturnValue().Set(scene != nullptr);
}

// obs.canvas.moveScene(currentCanvasName, sceneName, destCanvasName)
static void CanvasMoveScene(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    if (args.Length() < 3 || !args[0]->IsString() || !args[1]->IsString() || !args[2]->IsString()) {
        args.GetReturnValue().Set(false);
        return;
    }
    v8::String::Utf8Value currentCanvasName(isolate, args[0]);
    v8::String::Utf8Value sceneName(isolate, args[1]);
    v8::String::Utf8Value destCanvasName(isolate, args[2]);
    
    obs_canvas_t* currCanvas = obs_get_canvas_by_name(*currentCanvasName); // Not technically needed for the call but good for verification if validation needed
    obs_canvas_t* destCanvas = obs_get_canvas_by_name(*destCanvasName);
    obs_source_t* scene = obs_get_source_by_name(*sceneName);
    
    if (destCanvas && scene) {
        obs_scene_t* sceneObj = obs_scene_from_source(scene);
        if (sceneObj) {
            obs_canvas_move_scene(sceneObj, destCanvas);
        }
    }
    
    if (currCanvas) obs_canvas_release(currCanvas);
    if (destCanvas) obs_canvas_release(destCanvas);
    if (scene) obs_source_release(scene);
    
    args.GetReturnValue().Set(true);
}

// obs.canvas.getScene(canvasName, sceneName)
static void CanvasGetScene(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    if (args.Length() < 2 || !args[0]->IsString() || !args[1]->IsString()) {
        args.GetReturnValue().SetNull();
        return;
    }
    v8::String::Utf8Value canvasName(isolate, args[0]);
    v8::String::Utf8Value sceneName(isolate, args[1]);
    
    obs_canvas_t* canvas = obs_get_canvas_by_name(*canvasName);
    if (!canvas) {
        args.GetReturnValue().SetNull();
        return;
    }
    
    obs_scene_t* scene = obs_canvas_get_scene_by_name(canvas, *sceneName);
    if (scene) {
        obs_source_t* source = obs_scene_get_source(scene);
        const char* name = obs_source_get_name(source);
        args.GetReturnValue().Set(v8::String::NewFromUtf8(isolate, name ? name : "").ToLocalChecked());
    } else {
        args.GetReturnValue().SetNull();
    }
    
    obs_canvas_release(canvas);
}

// obs.canvas.getSource(canvasName, sourceName)
static void CanvasGetSource(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    if (args.Length() < 2 || !args[0]->IsString() || !args[1]->IsString()) {
        args.GetReturnValue().SetNull();
        return;
    }
    v8::String::Utf8Value canvasName(isolate, args[0]);
    v8::String::Utf8Value sourceName(isolate, args[1]);
    
    obs_canvas_t* canvas = obs_get_canvas_by_name(*canvasName);
    if (!canvas) {
        args.GetReturnValue().SetNull();
        return;
    }
    
    obs_source_t* source = obs_canvas_get_source_by_name(canvas, *sourceName);
    if (source) {
        // Just return the name to confirm existence/retrieval for now, 
        // or we could return a SourceInfo object. User just asked for getSource.
        // Let's return the name.
        const char* name = obs_source_get_name(source);
        args.GetReturnValue().Set(v8::String::NewFromUtf8(isolate, name ? name : "").ToLocalChecked());
        obs_source_release(source);
    } else {
        args.GetReturnValue().SetNull();
    }
    
    obs_canvas_release(canvas);
}

// obs.canvas.setVideoSettings(settings)
static void CanvasSetVideoSettings(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();

    if (args.Length() < 1 || !args[0]->IsObject()) {
        args.GetReturnValue().Set(false);
        return;
    }

    v8::Local<v8::Object> settings = args[0].As<v8::Object>();
    
    obs_video_info ovi;
    if (!obs_get_video_info(&ovi)) {
        args.GetReturnValue().Set(false);
        return;
    }

    // Helper to get uint32
    auto getUint = [&](const char* key, uint32_t& target) {
        v8::Local<v8::String> k = v8::String::NewFromUtf8(isolate, key).ToLocalChecked();
        if (settings->Has(context, k).FromMaybe(false)) {
            v8::Local<v8::Value> v = settings->Get(context, k).ToLocalChecked();
            if (v->IsNumber()) target = v->Uint32Value(context).FromMaybe(target);
        }
    };

    getUint("baseWidth", ovi.base_width);
    getUint("baseHeight", ovi.base_height);
    getUint("outputWidth", ovi.output_width);
    getUint("outputHeight", ovi.output_height);
    getUint("fpsNum", ovi.fps_num);
    getUint("fpsDen", ovi.fps_den);
    // TODO: Support colorspace, range, etc? For now, resolution and FPS are primary.

    int ret = obs_reset_video(&ovi);
    args.GetReturnValue().Set(ret == OBS_VIDEO_SUCCESS);
}

void SetupCanvasBindings(v8::Isolate* isolate, v8::Local<v8::Object> obs) {
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    v8::Local<v8::Object> canvas = v8::Object::New(isolate);
    
    auto setFunc = [&](const char* name, v8::FunctionCallback cb) {
        v8::Local<v8::Function> fn = v8::Function::New(context, cb).ToLocalChecked();
        canvas->Set(context, 
            v8::String::NewFromUtf8(isolate, name).ToLocalChecked(),
            fn
        ).Check();
    };
    
    // Legacy video info functions
    setFunc("getBaseResolution", GetBaseResolution);
    setFunc("getOutputResolution", GetOutputResolution);
    setFunc("getFps", GetFps);
    setFunc("getVideoInfo", GetVideoInfo);
    setFunc("getOutputs", GetOutputs);
    setFunc("getOutput", GetOutput);
    
    // OBS 31+ multi-canvas functions
    setFunc("getMain", CanvasGetMain);
    setFunc("list", CanvasList);
    setFunc("get", CanvasGet);
    setFunc("getScenes", CanvasGetScenes);
    setFunc("setName", CanvasSetName);

    // Advanced Canvas Lifecycle
    setFunc("create", CanvasCreate);
    setFunc("createPrivate", CanvasCreatePrivate);
    setFunc("remove", CanvasRemove);
    setFunc("save", CanvasSave);
    setFunc("load", CanvasLoad);

    // Advanced Canvas Scene Management
    setFunc("addScene", CanvasAddScene);
    setFunc("removeScene", CanvasRemoveScene);
    setFunc("moveScene", CanvasMoveScene);
    setFunc("getScene", CanvasGetScene);
    setFunc("getSource", CanvasGetSource);

    // Video Settings
    setFunc("setVideoSettings", CanvasSetVideoSettings);
    
    obs->Set(context,
        v8::String::NewFromUtf8(isolate, "canvas").ToLocalChecked(),
        canvas
    ).Check();
}

} // namespace obs_bindings

