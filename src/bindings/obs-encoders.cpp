/**
 * obs-encoders.cpp - OBS encoder bindings
 * 
 * Exposes obs.encoders API for managing video/audio encoders.
 */

#include "obs-bindings.h"
#include <obs.h>

namespace obs_bindings {

// obs.encoders.getVideoTypes() - Get available video encoder type IDs
static void EncodersGetVideoTypes(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    v8::Local<v8::Array> result = v8::Array::New(isolate);
    uint32_t index = 0;
    
    size_t i = 0;
    const char* id;
    while (obs_enum_encoder_types(i++, &id)) {
        obs_encoder_t* enc = obs_video_encoder_create(id, "temp", nullptr, nullptr);
        if (enc) {
            result->Set(context, index++,
                v8::String::NewFromUtf8(isolate, id).ToLocalChecked()
            ).Check();
            obs_encoder_release(enc);
        }
    }
    
    args.GetReturnValue().Set(result);
}

// obs.encoders.getAudioTypes() - Get available audio encoder type IDs
static void EncodersGetAudioTypes(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    v8::Local<v8::Array> result = v8::Array::New(isolate);
    uint32_t index = 0;
    
    size_t i = 0;
    const char* id;
    while (obs_enum_encoder_types(i++, &id)) {
        obs_encoder_t* enc = obs_audio_encoder_create(id, "temp", nullptr, 0, nullptr);
        if (enc) {
            result->Set(context, index++,
                v8::String::NewFromUtf8(isolate, id).ToLocalChecked()
            ).Check();
            obs_encoder_release(enc);
        }
    }
    
    args.GetReturnValue().Set(result);
}

// obs.encoders.list() - List all active encoders
static void EncodersList(const v8::FunctionCallbackInfo<v8::Value>& args) {
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
    
    obs_enum_encoders([](void* param, obs_encoder_t* encoder) -> bool {
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
        
        setStr("name", obs_encoder_get_name(encoder));
        setStr("id", obs_encoder_get_id(encoder));
        setStr("codec", obs_encoder_get_codec(encoder));
        setStr("type", obs_encoder_get_type(encoder) == OBS_ENCODER_VIDEO ? "video" : "audio");
        setNum("width", obs_encoder_get_width(encoder));
        setNum("height", obs_encoder_get_height(encoder));
        setNum("sampleRate", obs_encoder_get_sample_rate(encoder));
        setBool("active", obs_encoder_active(encoder));
        
        data->result->Set(data->context, data->index++, obj).Check();
        return true;
    }, &data);
    
    args.GetReturnValue().Set(result);
}

// obs.encoders.get(name) - Get encoder by name
static void EncodersGet(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    if (args.Length() < 1 || !args[0]->IsString()) {
        args.GetReturnValue().SetNull();
        return;
    }
    
    v8::String::Utf8Value name(isolate, args[0]);
    obs_encoder_t* encoder = obs_get_encoder_by_name(*name);
    
    if (!encoder) {
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
    
    setStr("name", obs_encoder_get_name(encoder));
    setStr("id", obs_encoder_get_id(encoder));
    setStr("codec", obs_encoder_get_codec(encoder));
    setStr("type", obs_encoder_get_type(encoder) == OBS_ENCODER_VIDEO ? "video" : "audio");
    setNum("width", obs_encoder_get_width(encoder));
    setNum("height", obs_encoder_get_height(encoder));
    setNum("sampleRate", obs_encoder_get_sample_rate(encoder));
    setBool("active", obs_encoder_active(encoder));
    
    obs_encoder_release(encoder);
    args.GetReturnValue().Set(obj);
}

void SetupEncoderBindings(v8::Isolate* isolate, v8::Local<v8::Object> obs) {
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    v8::Local<v8::Object> encoders = v8::Object::New(isolate);
    
    auto setFunc = [&](const char* name, v8::FunctionCallback cb) {
        v8::Local<v8::Function> fn = v8::Function::New(context, cb).ToLocalChecked();
        encoders->Set(context, 
            v8::String::NewFromUtf8(isolate, name).ToLocalChecked(),
            fn
        ).Check();
    };
    
    setFunc("getVideoTypes", EncodersGetVideoTypes);
    setFunc("getAudioTypes", EncodersGetAudioTypes);
    setFunc("list", EncodersList);
    setFunc("get", EncodersGet);
    
    obs->Set(context,
        v8::String::NewFromUtf8(isolate, "encoders").ToLocalChecked(),
        encoders
    ).Check();
}

} // namespace obs_bindings
