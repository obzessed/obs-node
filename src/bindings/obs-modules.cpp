/**
 * obs-modules.cpp - OBS module bindings
 * 
 * Exposes obs.modules API for getting information about loaded modules/plugins.
 */

#include "obs-bindings.h"
#include <obs.h>
#include <obs-module.h>
#include <vector>
#include <string>

namespace obs_bindings {

// obs.modules.list() - List all loaded module names
static void ModulesList(const v8::FunctionCallbackInfo<v8::Value>& args) {
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
    
    obs_enum_modules([](void* param, obs_module_t* module) {
        auto* data = static_cast<EnumData*>(param);
        const char* name = obs_get_module_name(module);
        
        v8::Local<v8::Object> obj = v8::Object::New(data->isolate);
        
        auto setStr = [&](const char* key, const char* val) {
            obj->Set(data->context,
                v8::String::NewFromUtf8(data->isolate, key).ToLocalChecked(),
                v8::String::NewFromUtf8(data->isolate, val ? val : "").ToLocalChecked()
            ).Check();
        };
        
        setStr("name", name);
        setStr("fileName", obs_get_module_file_name(module));
        setStr("author", obs_get_module_author(module));
        setStr("description", obs_get_module_description(module));
        setStr("binaryPath", obs_get_module_binary_path(module));
        setStr("dataPath", obs_get_module_data_path(module));
        
        data->result->Set(data->context, data->index++, obj).Check();
    }, &data);
    
    args.GetReturnValue().Set(result);
}

// obs.modules.get(fileName) - Get info for a specific module by file name
static void ModulesGet(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    if (args.Length() < 1 || !args[0]->IsString()) {
        args.GetReturnValue().SetNull();
        return;
    }
    
    v8::String::Utf8Value targetName(isolate, args[0]);
    
    struct FindData {
        const char* target;
        obs_module_t* found = nullptr;
    };
    
    FindData findData{*targetName, nullptr};
    
    obs_enum_modules([](void* param, obs_module_t* module) {
        auto* data = static_cast<FindData*>(param);
        const char* fileName = obs_get_module_file_name(module);
        const char* name = obs_get_module_name(module);
        
        if ((fileName && strcmp(fileName, data->target) == 0) ||
            (name && strcmp(name, data->target) == 0)) {
            data->found = module;
        }
    }, &findData);
    
    if (!findData.found) {
        args.GetReturnValue().SetNull();
        return;
    }
    
    obs_module_t* module = findData.found;
    v8::Local<v8::Object> obj = v8::Object::New(isolate);
    
    auto setStr = [&](const char* key, const char* val) {
        obj->Set(context,
            v8::String::NewFromUtf8(isolate, key).ToLocalChecked(),
            v8::String::NewFromUtf8(isolate, val ? val : "").ToLocalChecked()
        ).Check();
    };
    
    setStr("name", obs_get_module_name(module));
    setStr("fileName", obs_get_module_file_name(module));
    setStr("author", obs_get_module_author(module));
    setStr("description", obs_get_module_description(module));
    setStr("binaryPath", obs_get_module_binary_path(module));
    setStr("dataPath", obs_get_module_data_path(module));
    
    args.GetReturnValue().Set(obj);
}

// obs.modules.getDataPath(moduleName, file) - Get data file path for a module
static void ModulesGetDataPath(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    
    if (args.Length() < 2 || !args[0]->IsString() || !args[1]->IsString()) {
        args.GetReturnValue().SetNull();
        return;
    }
    
    v8::String::Utf8Value moduleName(isolate, args[0]);
    v8::String::Utf8Value fileName(isolate, args[1]);
    
    // Find the module
    struct FindData {
        const char* target;
        obs_module_t* found = nullptr;
    };
    
    FindData findData{*moduleName, nullptr};
    
    obs_enum_modules([](void* param, obs_module_t* module) {
        auto* data = static_cast<FindData*>(param);
        const char* name = obs_get_module_name(module);
        const char* fn = obs_get_module_file_name(module);
        
        if ((name && strcmp(name, data->target) == 0) ||
            (fn && strcmp(fn, data->target) == 0)) {
            data->found = module;
        }
    }, &findData);
    
    if (!findData.found) {
        args.GetReturnValue().SetNull();
        return;
    }
    
    char* path = obs_find_module_file(findData.found, *fileName);
    if (path) {
        args.GetReturnValue().Set(
            v8::String::NewFromUtf8(isolate, path).ToLocalChecked()
        );
        bfree(path);
    } else {
        args.GetReturnValue().SetNull();
    }
}

// obs.modules.getConfigPath(moduleName, file) - Get config file path for a module
static void ModulesGetConfigPath(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    
    if (args.Length() < 2 || !args[0]->IsString() || !args[1]->IsString()) {
        args.GetReturnValue().SetNull();
        return;
    }
    
    v8::String::Utf8Value moduleName(isolate, args[0]);
    v8::String::Utf8Value fileName(isolate, args[1]);
    
    // Find the module
    struct FindData {
        const char* target;
        obs_module_t* found = nullptr;
    };
    
    FindData findData{*moduleName, nullptr};
    
    obs_enum_modules([](void* param, obs_module_t* module) {
        auto* data = static_cast<FindData*>(param);
        const char* name = obs_get_module_name(module);
        const char* fn = obs_get_module_file_name(module);
        
        if ((name && strcmp(name, data->target) == 0) ||
            (fn && strcmp(fn, data->target) == 0)) {
            data->found = module;
        }
    }, &findData);
    
    if (!findData.found) {
        args.GetReturnValue().SetNull();
        return;
    }
    
    char* path = obs_module_get_config_path(findData.found, *fileName);
    if (path) {
        args.GetReturnValue().Set(
            v8::String::NewFromUtf8(isolate, path).ToLocalChecked()
        );
        bfree(path);
    } else {
        args.GetReturnValue().SetNull();
    }
}

void SetupModuleBindings(v8::Isolate* isolate, v8::Local<v8::Object> obs) {
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    v8::Local<v8::Object> modules = v8::Object::New(isolate);
    
    auto setFunc = [&](const char* name, v8::FunctionCallback cb) {
        v8::Local<v8::Function> fn = v8::Function::New(context, cb).ToLocalChecked();
        modules->Set(context, 
            v8::String::NewFromUtf8(isolate, name).ToLocalChecked(),
            fn
        ).Check();
    };
    
    setFunc("list", ModulesList);
    setFunc("get", ModulesGet);
    setFunc("getDataPath", ModulesGetDataPath);
    setFunc("getConfigPath", ModulesGetConfigPath);
    
    obs->Set(context,
        v8::String::NewFromUtf8(isolate, "modules").ToLocalChecked(),
        modules
    ).Check();
}

} // namespace obs_bindings
