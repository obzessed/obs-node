/**
 * obs-data.cpp - OBS data/settings bindings
 * 
 * Exposes obs.data API for manipulating obs_data_t settings.
 * Also provides getSettings/setSettings for sources.
 */

#include "obs-bindings.h"
#include <obs.h>

namespace obs_bindings {

// Helper to convert obs_data_t to JS object
v8::Local<v8::Object> ObsDataToJS(v8::Isolate* isolate, v8::Local<v8::Context> context, obs_data_t* data) {
    v8::Local<v8::Object> obj = v8::Object::New(isolate);
    
    if (!data) return obj;
    
    obs_data_item_t* item = obs_data_first(data);
    while (item) {
        const char* name = obs_data_item_get_name(item);
        v8::Local<v8::String> key = v8::String::NewFromUtf8(isolate, name).ToLocalChecked();
        
        switch (obs_data_item_gettype(item)) {
            case OBS_DATA_STRING:
                obj->Set(context, key,
                    v8::String::NewFromUtf8(isolate, obs_data_item_get_string(item)).ToLocalChecked()
                ).Check();
                break;
            case OBS_DATA_NUMBER:
                if (obs_data_item_numtype(item) == OBS_DATA_NUM_INT) {
                    obj->Set(context, key, v8::Number::New(isolate, 
                        static_cast<double>(obs_data_item_get_int(item)))).Check();
                } else {
                    obj->Set(context, key, v8::Number::New(isolate, 
                        obs_data_item_get_double(item))).Check();
                }
                break;
            case OBS_DATA_BOOLEAN:
                obj->Set(context, key, v8::Boolean::New(isolate, 
                    obs_data_item_get_bool(item))).Check();
                break;
            case OBS_DATA_OBJECT: {
                obs_data_t* sub = obs_data_item_get_obj(item);
                obj->Set(context, key, ObsDataToJS(isolate, context, sub)).Check();
                obs_data_release(sub);
                break;
            }
            case OBS_DATA_ARRAY: {
                obs_data_array_t* arr = obs_data_item_get_array(item);
                size_t count = obs_data_array_count(arr);
                v8::Local<v8::Array> jsArr = v8::Array::New(isolate, static_cast<int>(count));
                for (size_t i = 0; i < count; i++) {
                    obs_data_t* arrItem = obs_data_array_item(arr, i);
                    jsArr->Set(context, static_cast<uint32_t>(i), 
                        ObsDataToJS(isolate, context, arrItem)).Check();
                    obs_data_release(arrItem);
                }
                obj->Set(context, key, jsArr).Check();
                obs_data_array_release(arr);
                break;
            }
            default:
                break;
        }
        
        obs_data_item_next(&item);
    }
    
    return obj;
}

// Helper to convert JS object to obs_data_t
obs_data_t* JSToObsData(v8::Isolate* isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> obj) {
    obs_data_t* data = obs_data_create();
    
    v8::Local<v8::Array> keys = obj->GetOwnPropertyNames(context).ToLocalChecked();
    
    for (uint32_t i = 0; i < keys->Length(); i++) {
        v8::Local<v8::Value> keyVal = keys->Get(context, i).ToLocalChecked();
        v8::String::Utf8Value keyStr(isolate, keyVal);
        v8::Local<v8::Value> val = obj->Get(context, keyVal).ToLocalChecked();
        
        if (val->IsString()) {
            v8::String::Utf8Value str(isolate, val);
            obs_data_set_string(data, *keyStr, *str);
        } else if (val->IsNumber()) {
            double num = val->NumberValue(context).FromJust();
            if (num == static_cast<long long>(num)) {
                obs_data_set_int(data, *keyStr, static_cast<long long>(num));
            } else {
                obs_data_set_double(data, *keyStr, num);
            }
        } else if (val->IsBoolean()) {
            obs_data_set_bool(data, *keyStr, val->BooleanValue(isolate));
        } else if (val->IsObject() && !val->IsArray()) {
            obs_data_t* sub = JSToObsData(isolate, context, val.As<v8::Object>());
            obs_data_set_obj(data, *keyStr, sub);
            obs_data_release(sub);
        } else if (val->IsArray()) {
            v8::Local<v8::Array> arr = val.As<v8::Array>();
            obs_data_array_t* obsArr = obs_data_array_create();
            for (uint32_t j = 0; j < arr->Length(); j++) {
                v8::Local<v8::Value> arrVal = arr->Get(context, j).ToLocalChecked();
                if (arrVal->IsObject()) {
                    obs_data_t* arrData = JSToObsData(isolate, context, arrVal.As<v8::Object>());
                    obs_data_array_push_back(obsArr, arrData);
                    obs_data_release(arrData);
                }
            }
            obs_data_set_array(data, *keyStr, obsArr);
            obs_data_array_release(obsArr);
        }
    }
    
    return data;
}

// obs.data.getSourceSettings(sourceName) - Get source settings as JS object
static void DataGetSourceSettings(const v8::FunctionCallbackInfo<v8::Value>& args) {
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
    
    obs_data_t* settings = obs_source_get_settings(source);
    v8::Local<v8::Object> result = ObsDataToJS(isolate, context, settings);
    obs_data_release(settings);
    obs_source_release(source);
    
    args.GetReturnValue().Set(result);
}

// obs.data.setSourceSettings(sourceName, settings) - Set source settings from JS object
static void DataSetSourceSettings(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    if (args.Length() < 2 || !args[0]->IsString() || !args[1]->IsObject()) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    v8::String::Utf8Value name(isolate, args[0]);
    v8::Local<v8::Object> settings = args[1].As<v8::Object>();
    
    obs_source_t* source = obs_get_source_by_name(*name);
    if (!source) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    obs_data_t* data = JSToObsData(isolate, context, settings);
    obs_source_update(source, data);
    obs_data_release(data);
    obs_source_release(source);
    
    args.GetReturnValue().Set(true);
}

// obs.data.getFilterSettings(sourceName, filterName) - Get filter settings
static void DataGetFilterSettings(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    if (args.Length() < 2 || !args[0]->IsString() || !args[1]->IsString()) {
        args.GetReturnValue().SetNull();
        return;
    }
    
    v8::String::Utf8Value sourceName(isolate, args[0]);
    v8::String::Utf8Value filterName(isolate, args[1]);
    
    obs_source_t* source = obs_get_source_by_name(*sourceName);
    if (!source) {
        args.GetReturnValue().SetNull();
        return;
    }
    
    obs_source_t* filter = obs_source_get_filter_by_name(source, *filterName);
    if (!filter) {
        obs_source_release(source);
        args.GetReturnValue().SetNull();
        return;
    }
    
    obs_data_t* settings = obs_source_get_settings(filter);
    v8::Local<v8::Object> result = ObsDataToJS(isolate, context, settings);
    obs_data_release(settings);
    obs_source_release(filter);
    obs_source_release(source);
    
    args.GetReturnValue().Set(result);
}

// obs.data.setFilterSettings(sourceName, filterName, settings)
static void DataSetFilterSettings(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    if (args.Length() < 3 || !args[0]->IsString() || !args[1]->IsString() || !args[2]->IsObject()) {
        args.GetReturnValue().Set(false);
        return;
    }
    
    v8::String::Utf8Value sourceName(isolate, args[0]);
    v8::String::Utf8Value filterName(isolate, args[1]);
    v8::Local<v8::Object> settings = args[2].As<v8::Object>();
    
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
    
    obs_data_t* data = JSToObsData(isolate, context, settings);
    // obs_source_update merges the new settings with existing ones (does not replace all settings)
    obs_source_update(filter, data);
    obs_data_release(data);
    obs_source_release(filter);
    obs_source_release(source);
    
    args.GetReturnValue().Set(true);
}

// obs.data.toJSON(sourceName) - Get source settings as JSON string
static void DataToJSON(const v8::FunctionCallbackInfo<v8::Value>& args) {
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
    
    obs_data_t* settings = obs_source_get_settings(source);
    const char* json = obs_data_get_json(settings);
    
    args.GetReturnValue().Set(
        v8::String::NewFromUtf8(isolate, json ? json : "{}").ToLocalChecked()
    );
    
    obs_data_release(settings);
    obs_source_release(source);
}

void SetupDataBindings(v8::Isolate* isolate, v8::Local<v8::Object> obs) {
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    v8::Local<v8::Object> data = v8::Object::New(isolate);
    
    auto setFunc = [&](const char* name, v8::FunctionCallback cb) {
        v8::Local<v8::Function> fn = v8::Function::New(context, cb).ToLocalChecked();
        data->Set(context, 
            v8::String::NewFromUtf8(isolate, name).ToLocalChecked(),
            fn
        ).Check();
    };
    
    setFunc("getSourceSettings", DataGetSourceSettings);
    setFunc("setSourceSettings", DataSetSourceSettings);
    setFunc("getFilterSettings", DataGetFilterSettings);
    setFunc("setFilterSettings", DataSetFilterSettings);
    setFunc("toJSON", DataToJSON);
    
    obs->Set(context,
        v8::String::NewFromUtf8(isolate, "data").ToLocalChecked(),
        data
    ).Check();
}

} // namespace obs_bindings
