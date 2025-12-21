/**
 * obs-filters.cpp - OBS filter bindings
 * 
 * Exposes obs.filters API for managing filters on sources.
 */

#include "obs-bindings.h"
#include <obs.h>

namespace obs_bindings {

// obs.filters.list(sourceName) - List filters on a source
static void FiltersList(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    if (args.Length() < 1 || !args[0]->IsString()) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8(isolate, "Expected sourceName argument").ToLocalChecked()));
        return;
    }
    
    v8::String::Utf8Value sourceName(isolate, args[0]);
    obs_source_t* source = obs_get_source_by_name(*sourceName);
    
    if (!source) {
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
    
    obs_source_enum_filters(source, [](obs_source_t*, obs_source_t* filter, void* param) {
        auto* data = static_cast<EnumData*>(param);
        
        v8::Local<v8::Object> obj = v8::Object::New(data->isolate);
        
        const char* name = obs_source_get_name(filter);
        const char* id = obs_source_get_id(filter);
        
        obj->Set(data->context,
            v8::String::NewFromUtf8(data->isolate, "name").ToLocalChecked(),
            v8::String::NewFromUtf8(data->isolate, name ? name : "").ToLocalChecked()
        ).Check();
        
        obj->Set(data->context,
            v8::String::NewFromUtf8(data->isolate, "id").ToLocalChecked(),
            v8::String::NewFromUtf8(data->isolate, id ? id : "").ToLocalChecked()
        ).Check();
        
        obj->Set(data->context,
            v8::String::NewFromUtf8(data->isolate, "enabled").ToLocalChecked(),
            v8::Boolean::New(data->isolate, obs_source_enabled(filter))
        ).Check();
        
        data->result->Set(data->context, data->index++, obj).Check();
    }, &data);
    
    obs_source_release(source);
    args.GetReturnValue().Set(result);
}

// obs.filters.getTypes() - Get available filter type IDs
static void FiltersGetTypes(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    v8::Local<v8::Array> result = v8::Array::New(isolate);
    uint32_t index = 0;
    
    size_t i = 0;
    const char* id;
    while (obs_enum_filter_types(i++, &id)) {
        result->Set(context, index++,
            v8::String::NewFromUtf8(isolate, id).ToLocalChecked()
        ).Check();
    }
    
    args.GetReturnValue().Set(result);
}

// obs.filters.add(sourceName, filterName, filterTypeId) - Add a filter
static void FiltersAdd(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    
    if (args.Length() < 3 || !args[0]->IsString() || !args[1]->IsString() || !args[2]->IsString()) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    v8::String::Utf8Value sourceName(isolate, args[0]);
    v8::String::Utf8Value filterName(isolate, args[1]);
    v8::String::Utf8Value filterTypeId(isolate, args[2]);
    
    obs_source_t* source = obs_get_source_by_name(*sourceName);
    if (!source) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    // Create the filter
    obs_source_t* filter = obs_source_create(*filterTypeId, *filterName, nullptr, nullptr);
    if (!filter) {
        obs_source_release(source);
        args.GetReturnValue().Set(false);
        return;
    }
    
    obs_source_filter_add(source, filter);
    obs_source_release(filter);
    obs_source_release(source);
    
    args.GetReturnValue().Set(true);
}

// obs.filters.remove(sourceName, filterName) - Remove a filter
static void FiltersRemove(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    
    if (args.Length() < 2 || !args[0]->IsString() || !args[1]->IsString()) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    v8::String::Utf8Value sourceName(isolate, args[0]);
    v8::String::Utf8Value filterName(isolate, args[1]);
    
    obs_source_t* source = obs_get_source_by_name(*sourceName);
    if (!source) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    obs_source_t* filter = obs_source_get_filter_by_name(source, *filterName);
    if (!filter) {
        obs_source_release(source);
        args.GetReturnValue().Set(false);
        return;
    }
    
    obs_source_filter_remove(source, filter);
    obs_source_release(filter);
    obs_source_release(source);
    
    args.GetReturnValue().Set(true);
}

// obs.filters.setEnabled(sourceName, filterName, enabled) - Enable/disable filter
static void FiltersSetEnabled(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    
    if (args.Length() < 3 || !args[0]->IsString() || !args[1]->IsString() || !args[2]->IsBoolean()) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    v8::String::Utf8Value sourceName(isolate, args[0]);
    v8::String::Utf8Value filterName(isolate, args[1]);
    bool enabled = args[2]->BooleanValue(isolate);
    
    obs_source_t* source = obs_get_source_by_name(*sourceName);
    if (!source) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    obs_source_t* filter = obs_source_get_filter_by_name(source, *filterName);
    if (!filter) {
        obs_source_release(source);
        args.GetReturnValue().Set(false);
        return;
    }
    
    obs_source_set_enabled(filter, enabled);
    obs_source_release(filter);
    obs_source_release(source);
    
    args.GetReturnValue().Set(true);
}

// obs.filters.reorder(sourceName, filterName, newIndex) - Reorder a filter
static void FiltersReorder(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    if (args.Length() < 3 || !args[0]->IsString() || !args[1]->IsString() || !args[2]->IsNumber()) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    v8::String::Utf8Value sourceName(isolate, args[0]);
    v8::String::Utf8Value filterName(isolate, args[1]);
    int newIndex = args[2]->Int32Value(context).FromJust();
    
    obs_source_t* source = obs_get_source_by_name(*sourceName);
    if (!source) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    obs_source_t* filter = obs_source_get_filter_by_name(source, *filterName);
    if (!filter) {
        obs_source_release(source);
        args.GetReturnValue().Set(false);
        return;
    }
    
    obs_source_filter_set_order(source, filter, 
        newIndex == 0 ? OBS_ORDER_MOVE_TOP : OBS_ORDER_MOVE_DOWN);
    
    obs_source_release(filter);
    obs_source_release(source);
    
    args.GetReturnValue().Set(true);
}

void SetupFilterBindings(v8::Isolate* isolate, v8::Local<v8::Object> obs) {
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    v8::Local<v8::Object> filters = v8::Object::New(isolate);
    
    auto setFunc = [&](const char* name, v8::FunctionCallback cb) {
        v8::Local<v8::Function> fn = v8::Function::New(context, cb).ToLocalChecked();
        filters->Set(context, 
            v8::String::NewFromUtf8(isolate, name).ToLocalChecked(),
            fn
        ).Check();
    };
    
    setFunc("list", FiltersList);
    setFunc("getTypes", FiltersGetTypes);
    setFunc("add", FiltersAdd);
    setFunc("remove", FiltersRemove);
    setFunc("setEnabled", FiltersSetEnabled);
    setFunc("reorder", FiltersReorder);
    
    obs->Set(context,
        v8::String::NewFromUtf8(isolate, "filters").ToLocalChecked(),
        filters
    ).Check();
}

} // namespace obs_bindings
