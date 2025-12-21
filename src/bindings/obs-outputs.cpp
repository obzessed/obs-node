/**
 * obs-outputs.cpp - OBS output bindings
 * 
 * Exposes obs.outputs API for managing streaming/recording outputs.
 */

#include "obs-bindings.h"
#include <obs.h>
#include <obs-frontend-api.h>

namespace obs_bindings {

// obs.outputs.list() - List all outputs
static void OutputsList(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    struct EnumData {
        v8::Isolate* isolate;
        v8::Local<v8::Context> context;
        v8::Local<v8::Array> result;
        uint32_t index = 0;
    };
    
    v8::Local<v8::Array> result = v8::Array::New(isolate);
    EnumData data{isolate, context, result, 0};
    
    obs_enum_outputs([](void* param, obs_output_t* output) -> bool {
        auto* data = static_cast<EnumData*>(param);
        
        v8::Local<v8::Object> obj = v8::Object::New(data->isolate);
        
        auto setStr = [&](const char* key, const char* val) {
            obj->Set(data->context,
                v8::String::NewFromUtf8(data->isolate, key).ToLocalChecked(),
                v8::String::NewFromUtf8(data->isolate, val ? val : "").ToLocalChecked()
            ).Check();
        };
        
        auto setNum = [&](const char* key, double val) {
            obj->Set(data->context,
                v8::String::NewFromUtf8(data->isolate, key).ToLocalChecked(),
                v8::Number::New(data->isolate, val)
            ).Check();
        };
        
        auto setBool = [&](const char* key, bool val) {
            obj->Set(data->context,
                v8::String::NewFromUtf8(data->isolate, key).ToLocalChecked(),
                v8::Boolean::New(data->isolate, val)
            ).Check();
        };
        
        setStr("name", obs_output_get_name(output));
        setStr("id", obs_output_get_id(output));
        setNum("width", obs_output_get_width(output));
        setNum("height", obs_output_get_height(output));
        setBool("active", obs_output_active(output));
        setBool("reconnecting", obs_output_reconnecting(output));
        setNum("totalFrames", obs_output_get_total_frames(output));
        setNum("droppedFrames", obs_output_get_frames_dropped(output));
        setNum("totalBytes", static_cast<double>(obs_output_get_total_bytes(output)));
        
        data->result->Set(data->context, data->index++, obj).Check();
        return true;
    }, &data);
    
    args.GetReturnValue().Set(result);
}

// obs.outputs.get(name) - Get output by name
static void OutputsGet(const v8::FunctionCallbackInfo<v8::Value>& args) {
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
    
    auto setStr = [&](const char* key, const char* val) {
        obj->Set(context,
            v8::String::NewFromUtf8(isolate, key).ToLocalChecked(),
            v8::String::NewFromUtf8(isolate, val ? val : "").ToLocalChecked()
        ).Check();
    };
    
    auto setNum = [&](const char* key, double val) {
        obj->Set(context,
            v8::String::NewFromUtf8(isolate, key).ToLocalChecked(),
            v8::Number::New(isolate, val)
        ).Check();
    };
    
    auto setBool = [&](const char* key, bool val) {
        obj->Set(context,
            v8::String::NewFromUtf8(isolate, key).ToLocalChecked(),
            v8::Boolean::New(isolate, val)
        ).Check();
    };
    
    setStr("name", obs_output_get_name(output));
    setStr("id", obs_output_get_id(output));
    setNum("width", obs_output_get_width(output));
    setNum("height", obs_output_get_height(output));
    setBool("active", obs_output_active(output));
    setBool("reconnecting", obs_output_reconnecting(output));
    setNum("totalFrames", obs_output_get_total_frames(output));
    setNum("droppedFrames", obs_output_get_frames_dropped(output));
    setNum("totalBytes", static_cast<double>(obs_output_get_total_bytes(output)));
    setNum("congestion", obs_output_get_congestion(output));
    
    obs_output_release(output);
    args.GetReturnValue().Set(obj);
}

// obs.outputs.getTypes() - Get available output type IDs
static void OutputsGetTypes(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    v8::Local<v8::Array> result = v8::Array::New(isolate);
    uint32_t index = 0;
    
    size_t i = 0;
    const char* id;
    while (obs_enum_output_types(i++, &id)) {
        result->Set(context, index++,
            v8::String::NewFromUtf8(isolate, id).ToLocalChecked()
        ).Check();
    }
    
    args.GetReturnValue().Set(result);
}

// obs.outputs.start(name) - Start an output
static void OutputsStart(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    
    if (args.Length() < 1 || !args[0]->IsString()) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    v8::String::Utf8Value name(isolate, args[0]);
    obs_output_t* output = obs_get_output_by_name(*name);
    
    if (!output) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    bool result = obs_output_start(output);
    obs_output_release(output);
    args.GetReturnValue().Set(result);
}

// obs.outputs.stop(name) - Stop an output
static void OutputsStop(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    
    if (args.Length() < 1 || !args[0]->IsString()) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    v8::String::Utf8Value name(isolate, args[0]);
    obs_output_t* output = obs_get_output_by_name(*name);
    
    if (!output) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    obs_output_stop(output);
    obs_output_release(output);
    args.GetReturnValue().Set(true);
}

// obs.outputs.isActive(name) - Check if output is active
static void OutputsIsActive(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    
    if (args.Length() < 1 || !args[0]->IsString()) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    v8::String::Utf8Value name(isolate, args[0]);
    obs_output_t* output = obs_get_output_by_name(*name);
    
    if (!output) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    bool active = obs_output_active(output);
    obs_output_release(output);
    args.GetReturnValue().Set(active);
}

void SetupOutputBindings(v8::Isolate* isolate, v8::Local<v8::Object> obs) {
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    v8::Local<v8::Object> outputs = v8::Object::New(isolate);
    
    auto setFunc = [&](const char* name, v8::FunctionCallback cb) {
        v8::Local<v8::Function> fn = v8::Function::New(context, cb).ToLocalChecked();
        outputs->Set(context, 
            v8::String::NewFromUtf8(isolate, name).ToLocalChecked(),
            fn
        ).Check();
    };
    
    setFunc("list", OutputsList);
    setFunc("get", OutputsGet);
    setFunc("getTypes", OutputsGetTypes);
    setFunc("start", OutputsStart);
    setFunc("stop", OutputsStop);
    setFunc("isActive", OutputsIsActive);
    
    obs->Set(context,
        v8::String::NewFromUtf8(isolate, "outputs").ToLocalChecked(),
        outputs
    ).Check();
}

} // namespace obs_bindings
