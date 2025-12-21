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
    
    obs->Set(context,
        v8::String::NewFromUtf8(isolate, "canvas").ToLocalChecked(),
        canvas
    ).Check();
}

} // namespace obs_bindings

