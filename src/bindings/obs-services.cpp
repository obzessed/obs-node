/**
 * obs-services.cpp - OBS service bindings
 * 
 * Exposes obs.services API for managing streaming services.
 */

#include "obs-bindings.h"
#include <obs.h>
#include <obs-frontend-api.h>

namespace obs_bindings {

// obs.services.getTypes() - Get available service type IDs
static void ServicesGetTypes(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    v8::Local<v8::Array> result = v8::Array::New(isolate);
    uint32_t index = 0;
    
    size_t i = 0;
    const char* id;
    while (obs_enum_service_types(i++, &id)) {
        result->Set(context, index++,
            v8::String::NewFromUtf8(isolate, id).ToLocalChecked()
        ).Check();
    }
    
    args.GetReturnValue().Set(result);
}

// obs.services.getCurrent() - Get current streaming service info
static void ServicesGetCurrent(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    obs_service_t* service = obs_frontend_get_streaming_service();
    if (!service) {
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
    
    setStr("name", obs_service_get_name(service));
    setStr("id", obs_service_get_id(service));
    setStr("type", obs_service_get_type(service));
    setStr("url", obs_service_get_connect_info(service, OBS_SERVICE_CONNECT_INFO_SERVER_URL));
    setStr("key", obs_service_get_connect_info(service, OBS_SERVICE_CONNECT_INFO_STREAM_KEY));
    
    obs_service_release(service);
    args.GetReturnValue().Set(obj);
}

// obs.services.list() - List all services
static void ServicesList(const v8::FunctionCallbackInfo<v8::Value>& args) {
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
    
    obs_enum_services([](void* param, obs_service_t* service) -> bool {
        auto* data = static_cast<EnumData*>(param);
        
        v8::Local<v8::Object> obj = v8::Object::New(data->isolate);
        
        auto setStr = [&](const char* key, const char* val) {
            obj->Set(data->context,
                v8::String::NewFromUtf8(data->isolate, key).ToLocalChecked(),
                v8::String::NewFromUtf8(data->isolate, val ? val : "").ToLocalChecked()
            ).Check();
        };
        
        setStr("name", obs_service_get_name(service));
        setStr("id", obs_service_get_id(service));
        setStr("type", obs_service_get_type(service));
        
        data->result->Set(data->context, data->index++, obj).Check();
        return true;
    }, &data);
    
    args.GetReturnValue().Set(result);
}

void SetupServiceBindings(v8::Isolate* isolate, v8::Local<v8::Object> obs) {
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    v8::Local<v8::Object> services = v8::Object::New(isolate);
    
    auto setFunc = [&](const char* name, v8::FunctionCallback cb) {
        v8::Local<v8::Function> fn = v8::Function::New(context, cb).ToLocalChecked();
        services->Set(context, 
            v8::String::NewFromUtf8(isolate, name).ToLocalChecked(),
            fn
        ).Check();
    };
    
    setFunc("getTypes", ServicesGetTypes);
    setFunc("getCurrent", ServicesGetCurrent);
    setFunc("list", ServicesList);
    
    obs->Set(context,
        v8::String::NewFromUtf8(isolate, "services").ToLocalChecked(),
        services
    ).Check();
}

} // namespace obs_bindings
