/**
 * obs-audio.cpp - OBS audio bindings
 * 
 * Exposes obs.audio API for global audio settings and source audio control.
 */

#include "obs-bindings.h"
#include <obs.h>
#include <obs-frontend-api.h>

namespace obs_bindings {

// obs.audio.getMonitoringDevice()
static void AudioGetMonitoringDevice(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    
    const char* name = nullptr;
    const char* id = nullptr;
    obs_get_audio_monitoring_device(&name, &id);
    
    // Return ID as the main identifier
    args.GetReturnValue().Set(
        v8::String::NewFromUtf8(isolate, id ? id : "Default").ToLocalChecked()
    );
}

// obs.audio.setMonitoringDevice(name, id)
static void AudioSetMonitoringDevice(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    
    if (args.Length() < 2 || !args[0]->IsString() || !args[1]->IsString()) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    v8::String::Utf8Value name(isolate, args[0]);
    v8::String::Utf8Value id(isolate, args[1]);
    
    obs_set_audio_monitoring_device(*name, *id);
    args.GetReturnValue().Set(true);
}

// obs.audio.setSourceVolume(sourceName, vol) - Volume in dB
static void AudioSetSourceVolume(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    
    if (args.Length() < 2 || !args[0]->IsString() || !args[1]->IsNumber()) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    v8::String::Utf8Value name(isolate, args[0]);
    double db = args[1]->NumberValue(isolate->GetCurrentContext()).FromJust();
    
    obs_source_t* source = obs_get_source_by_name(*name);
    if (!source) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    obs_source_set_volume(source, (float)obs_db_to_mul((float)db));
    obs_source_release(source);
    args.GetReturnValue().Set(true);
}

// obs.audio.getSourceVolume(sourceName) - Returns dB
static void AudioGetSourceVolume(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    
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
    
    float mul = obs_source_get_volume(source);
    float db = obs_mul_to_db(mul);
    
    obs_source_release(source);
    args.GetReturnValue().Set(v8::Number::New(isolate, db));
}

// obs.audio.setSourceSyncOffset(sourceName, offsetNs)
static void AudioSetSourceSyncOffset(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    
    if (args.Length() < 2 || !args[0]->IsString() || !args[1]->IsNumber()) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    v8::String::Utf8Value name(isolate, args[0]);
    int64_t offset = (int64_t)args[1]->NumberValue(isolate->GetCurrentContext()).FromJust();
    
    obs_source_t* source = obs_get_source_by_name(*name);
    if (!source) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    obs_source_set_sync_offset(source, offset);
    obs_source_release(source);
    args.GetReturnValue().Set(true);
}

// obs.audio.setSourceMonitoringType(sourceName, type) // 0=None, 1=Monitor Only, 2=Monitor+Output
static void AudioSetSourceMonitoringType(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    
    if (args.Length() < 2 || !args[0]->IsString() || !args[1]->IsNumber()) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    v8::String::Utf8Value name(isolate, args[0]);
    int type = args[1]->Int32Value(isolate->GetCurrentContext()).FromJust();
    
    obs_source_t* source = obs_get_source_by_name(*name);
    if (!source) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    obs_source_set_monitoring_type(source, (obs_monitoring_type)type);
    obs_source_release(source);
    args.GetReturnValue().Set(true);
}

// obs.audio.getSourceMonitoringType(sourceName)
static void AudioGetSourceMonitoringType(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    
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
    
    int type = (int)obs_source_get_monitoring_type(source);
    
    obs_source_release(source);
    args.GetReturnValue().Set(v8::Integer::New(isolate, type));
}

void SetupAudioBindings(v8::Isolate* isolate, v8::Local<v8::Object> obs) {
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    v8::Local<v8::Object> audio = v8::Object::New(isolate);
    
    auto setFunc = [&](const char* name, v8::FunctionCallback cb) {
        v8::Local<v8::Function> fn = v8::Function::New(context, cb).ToLocalChecked();
        audio->Set(context, 
            v8::String::NewFromUtf8(isolate, name).ToLocalChecked(),
            fn
        ).Check();
    };
    
    setFunc("getMonitoringDevice", AudioGetMonitoringDevice);
    setFunc("setMonitoringDevice", AudioSetMonitoringDevice);
    setFunc("setSourceVolume", AudioSetSourceVolume);
    setFunc("getSourceVolume", AudioGetSourceVolume);
    setFunc("setSourceSyncOffset", AudioSetSourceSyncOffset);
    setFunc("setSourceMonitoringType", AudioSetSourceMonitoringType);
    setFunc("getSourceMonitoringType", AudioGetSourceMonitoringType);
    
    obs->Set(context,
        v8::String::NewFromUtf8(isolate, "audio").ToLocalChecked(),
        audio
    ).Check();
}

} // namespace obs_bindings
