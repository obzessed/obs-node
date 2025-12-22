/**
 * obs-websocket.cpp - OBS WebSocket vendor bindings
 * 
 * Uses OBS proc_handler API to interact with obs-websocket plugin.
 * Reference: https://github.com/WarmUpTill/SceneSwitcher/blob/master/deps/obs-websocket/lib/obs-websocket-api.h
 */

#include "obs-bindings.h"
#include <util/bmem.h>
#include <string>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <util/config-file.h>
#include <obs-frontend-api.h>
#include "obs-websocket-api.h"
#include "plugin-support.h"

namespace obs_bindings {

// ============================================================================
// Globals & Types
// ============================================================================

static std::mutex g_requestMutex;
static std::mutex g_eventMutex; 

struct RequestCallback {
    v8::Global<v8::Function> callback;
    v8::Global<v8::Context> context;
    v8::Isolate* isolate;
    std::string requestType;
};

struct EventListener {
    v8::Global<v8::Function> callback;
    v8::Global<v8::Context> context;
    v8::Isolate* isolate;
    std::string eventType;
};

static std::unordered_map<std::string, RequestCallback*> g_requestCallbacks;
static std::unordered_map<std::string, std::vector<EventListener*>> g_eventCallbacks;
static std::unordered_map<std::string, obs_websocket_vendor> g_vendors;
static bool g_eventListenerRegistered = false;

// ============================================================================
// Internal Helpers
// ============================================================================

static obs_websocket_vendor GetOrRegisterVendor(const char* name) {
    std::lock_guard lock(g_requestMutex);
    
    auto it = g_vendors.find(name);
    if (it != g_vendors.end()) return it->second;
    
    obs_websocket_vendor vendor = obs_websocket_register_vendor(name);
    
    if (vendor) {
        g_vendors[name] = vendor;
    }
    return vendor;
}

static bool EmitVendorEvent(const char *vendorName, const char *eventName, obs_data_t *eventData) {
    obs_websocket_vendor vendor = GetOrRegisterVendor(vendorName);
    if (!vendor) return false;
    
    return obs_websocket_vendor_emit_event(vendor, eventName, eventData);
}

static v8::Local<v8::Object> CallWebSocketRequest(v8::Isolate* isolate, v8::Local<v8::Context> context, const char* type, obs_data_t* request_data) {
    v8::Local<v8::Object> result = v8::Object::New(isolate);

    obs_websocket_request_response *resp = obs_websocket_call_request(type, request_data);
    
    if (resp) {
        result->Set(context, 
            v8::String::NewFromUtf8(isolate, "status").ToLocalChecked(), 
            v8::Integer::New(isolate, resp->status_code)
        ).Check();
        
        if (resp->comment) {
            result->Set(context, 
                v8::String::NewFromUtf8(isolate, "comment").ToLocalChecked(), 
                v8::String::NewFromUtf8(isolate, resp->comment).ToLocalChecked()
            ).Check();
        }
        
        if (resp->response_data) {
	    if (obs_data_t *json_data = obs_data_create_from_json(resp->response_data)) {
                result->Set(context, 
                    v8::String::NewFromUtf8(isolate, "data").ToLocalChecked(), 
                    ObsDataToJS(isolate, context, json_data)
                ).Check();
                obs_data_release(json_data);
            }
        }
        
        obs_websocket_request_response_free(resp);
    } else {
        // Failed
        result->Set(context, 
            v8::String::NewFromUtf8(isolate, "status").ToLocalChecked(), 
            v8::Integer::New(isolate, 0)
        ).Check();
        result->Set(context,
             v8::String::NewFromUtf8(isolate, "comment").ToLocalChecked(),
             v8::String::NewFromUtf8(isolate, "Failed to call obs-websocket request (not installed?)").ToLocalChecked()
        ).Check();
    }
    
    return result;
}

// ============================================================================
// Thunks and Registrations
// ============================================================================

static void OnVendorRequestThunk(obs_data_t *request_data, obs_data_t *response_data, void *priv_data) {
    auto* cb = (RequestCallback*)priv_data;
    if (!cb) return;

    v8::Isolate* isolate = cb->isolate;
    v8::Locker locker(isolate);
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    
    v8::Local<v8::Context> context = cb->context.Get(isolate);
    v8::Context::Scope context_scope(context);

    v8::Local<v8::Function> callback = cb->callback.Get(isolate);
    
    v8::Local<v8::Object> jsReq = ObsDataToJS(isolate, context, request_data);
    v8::Local<v8::Value> argv[] = { jsReq };
    
    v8::TryCatch try_catch(isolate);
    v8::MaybeLocal<v8::Value> result = callback->Call(context, context->Global(), 1, argv);
    
    if (try_catch.HasCaught()) {
        v8::String::Utf8Value msg(isolate, try_catch.Message()->Get());
        blog(LOG_ERROR, "[obs-websocket] JS Callback error: %s", *msg);
    } else if (!result.IsEmpty()) {
        v8::Local<v8::Value> val = result.ToLocalChecked();
        if (val->IsObject()) {
            obs_data_t* retData = JSToObsData(isolate, context, val.As<v8::Object>());
            obs_data_apply(response_data, retData);
            obs_data_release(retData);
        }
    }
}

static bool RegisterVendorRequest(const char *vendorName, const char *requestType, v8::Isolate* isolate, v8::Local<v8::Context> context, v8::Local<v8::Function> cb) {
    obs_websocket_vendor vendor = GetOrRegisterVendor(vendorName);
    if (!vendor) return false;
    
    std::string key = std::string(vendorName) + "::" + requestType;
    
    std::lock_guard lock(g_requestMutex);
    
    if (g_requestCallbacks.find(key) != g_requestCallbacks.end()) {
        RequestCallback* old = g_requestCallbacks[key];
        old->callback.Reset(isolate, cb);
        old->context.Reset(isolate, context);
        return true; 
    }
    
    RequestCallback* rc = new RequestCallback();
    rc->isolate = isolate;
    rc->callback.Reset(isolate, cb);
    rc->context.Reset(isolate, context);
    rc->requestType = requestType;
    
    g_requestCallbacks[key] = rc; // Add to map first
    
    bool success = obs_websocket_vendor_register_request(vendor, requestType, OnVendorRequestThunk, rc);

    if (!success) {
        // If registration failed, clean up
        g_requestCallbacks.erase(key);
        delete rc;
    }
    
    return success;
}

static void OnWebSocketEventThunk(uint64_t /*param1*/, const char *event_type, const char *event_data_json, void *priv_data) {
    std::lock_guard lock(g_eventMutex);
    if (g_eventCallbacks.empty()) return;

    std::string type = event_type ? event_type : "";
    auto it = g_eventCallbacks.find(type);
    if (it == g_eventCallbacks.end()) return;
    
    for (auto* listener : it->second) {
        v8::Isolate* isolate = listener->isolate;
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handle_scope(isolate);
        
        v8::Local<v8::Context> context = listener->context.Get(isolate);
        v8::Context::Scope context_scope(context);
        
        v8::Local<v8::Function> cb = listener->callback.Get(isolate);
        
        // Parse JSON data
        v8::Local<v8::Value> jsData;
        if (event_data_json) {
             obs_data_t* data = obs_data_create_from_json(event_data_json);
             if (data) {
                 jsData = ObsDataToJS(isolate, context, data);
                 obs_data_release(data);
             } else {
                 jsData = v8::Null(isolate);
             }
        } else {
             jsData = v8::Null(isolate);
        }
        
        v8::Local<v8::Value> argv[] = { jsData };
        
        v8::TryCatch try_catch(isolate);
        cb->Call(context, context->Global(), 1, argv);
        
         if (try_catch.HasCaught()) {
            v8::String::Utf8Value msg(isolate, try_catch.Message()->Get());
            blog(LOG_ERROR, "[obs-websocket] JS Event Callback error: %s", *msg);
        }
    }
}

static void EnsureEventListenerRegistered() {
    if (g_eventListenerRegistered) return;
    
    // Use API function
    if (obs_websocket_register_event_callback(OnWebSocketEventThunk, nullptr)) g_eventListenerRegistered = true;
}

static void AddEventListener(const char* type, v8::Isolate* isolate, v8::Local<v8::Context> context, v8::Local<v8::Function> cb) {
    std::lock_guard lock(g_eventMutex);
    EnsureEventListenerRegistered();
    
    auto* el = new EventListener();
    el->isolate = isolate;
    el->callback.Reset(isolate, cb);
    el->context.Reset(isolate, context);
    el->eventType = type;
    
    g_eventCallbacks[type].push_back(el);
}

// ============================================================================
// JS Bindings
// ============================================================================

static void WebSocketEmit(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    if (args.Length() < 3 || !args[0]->IsString() || !args[1]->IsString() || !args[2]->IsObject()) {
         args.GetReturnValue().Set(false);
         return;
    }
    
    v8::String::Utf8Value vendorName(isolate, args[0]);
    v8::String::Utf8Value eventName(isolate, args[1]);
    v8::Local<v8::Object> dataObj = args[2].As<v8::Object>();
    
    obs_data_t* eventData = JSToObsData(isolate, context, dataObj);
    bool success = EmitVendorEvent(*vendorName, *eventName, eventData);
    obs_data_release(eventData);
    
    args.GetReturnValue().Set(success);
}

static void WebSocketOn(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    if (args.Length() == 2 && args[0]->IsString() && args[1]->IsFunction()) {
        v8::String::Utf8Value type(isolate, args[0]);
        v8::Local<v8::Function> cb = args[1].As<v8::Function>();
        AddEventListener(*type, isolate, context, cb);
        args.GetReturnValue().Set(true);
        return;
    }
    
    if (args.Length() == 3 && args[0]->IsString() && args[1]->IsString() && args[2]->IsFunction()) {
        v8::String::Utf8Value vendorName(isolate, args[0]);
        v8::String::Utf8Value requestType(isolate, args[1]);
        v8::Local<v8::Function> callback = args[2].As<v8::Function>();
        
        bool success = RegisterVendorRequest(*vendorName, *requestType, isolate, context, callback);
        args.GetReturnValue().Set(success);
        return;
    }
    
    args.GetReturnValue().Set(false);
}

static void WebSocketCall(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    if (args.Length() < 1 || !args[0]->IsString()) {
         args.GetReturnValue().Set(v8::Undefined(isolate));
         return;
    }
    
    v8::String::Utf8Value type(isolate, args[0]);
    obs_data_t* data = nullptr;
    
    if (args.Length() > 1 && args[1]->IsObject()) {
        data = JSToObsData(isolate, context, args[1].As<v8::Object>());
    }
    
    v8::Local<v8::Object> result = CallWebSocketRequest(isolate, context, *type, data);
    
    if (data) obs_data_release(data);
    
    args.GetReturnValue().Set(result);
}



static void WebSocketGetServerInfo(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Object> result = v8::Object::New(isolate);
    
    // 1. Runtime Info (API Version)
    unsigned int apiVer = obs_websocket_get_api_version();
    // bool running = (apiVer > 0);
    
    // result->Set(context, v8::String::NewFromUtf8(isolate, "running").ToLocalChecked(), v8::Boolean::New(isolate, running)).Check();
    result->Set(context, v8::String::NewFromUtf8(isolate, "rpcVersion").ToLocalChecked(), v8::Integer::New(isolate, apiVer)).Check();

    // 2. Config Info
    bool configFound = false;
    bool enabled = false;
    int port = 0; 
    bool portFound = false;
    bool auth = false;
    const char* password = nullptr;

    // A. Try Module Config (obs-websocket 5.x standard)
    obs_module_t *mod = obs_get_module("obs-websocket");
    bool available = (mod != nullptr);

    result->Set(context, v8::String::NewFromUtf8(isolate, "available").ToLocalChecked(), v8::Boolean::New(isolate, available)).Check();

    if (mod) {
        char *path = obs_module_get_config_path(mod, "config.json");
        if (path) {
            obs_data_t *config = obs_data_create_from_json_file(path);
            if (config) {
                // If config file exists, use it.
                // 5.x uses snake_case keys in json
                enabled = obs_data_get_bool(config, "server_enabled");
                port = (int)obs_data_get_int(config, "server_port");
                portFound = true;
                auth = obs_data_get_bool(config, "auth_required");
                password = obs_data_get_string(config, "server_password");
                configFound = true;
                obs_data_release(config);
            }
            bfree(path);
        }
    }
    
    // B. Fallback to App Config (Legacy or Global Config)
    if (!configFound) {
        config_t *global = obs_frontend_get_app_config();
        if (global) {
             const char* section = "ObsWebSocket";
             // Check if section exists or usually populated keys exist
             // Check CamelCase (older versions) and snake_case
             
             // Simple helper to check config
             auto has_val = [&](const char* s, const char* k) { return config_has_user_value(global, s, k); };
             
             bool hasSection = has_val(section, "ServerPort") || has_val(section, "server_port") || has_val(section, "ServerEnabled");
             if (!hasSection) {
                 if (has_val("obs-websocket", "server_port") || has_val("obs-websocket", "server_enabled")) {
                     section = "obs-websocket";
                     hasSection = true;
                 }
             }
             
             if (hasSection) {
                 // Enabled
                 if (has_val(section, "ServerEnabled")) enabled = config_get_bool(global, section, "ServerEnabled");
                 else if (has_val(section, "server_enabled")) enabled = config_get_bool(global, section, "server_enabled");
                 
                 // Port
                 if (has_val(section, "ServerPort")) {
                     port = (int)config_get_int(global, section, "ServerPort");
                     portFound = true;
                 } else if (has_val(section, "server_port")) {
                     port = (int)config_get_int(global, section, "server_port");
                     portFound = true;
                 }
                 
                 // Auth
                 if (has_val(section, "AuthRequired")) auth = config_get_bool(global, section, "AuthRequired");
                 else if (has_val(section, "auth_required")) auth = config_get_bool(global, section, "auth_required");

                 // Password
                 if (has_val(section, "ServerPassword")) password = config_get_string(global, section, "ServerPassword");
                 else if (has_val(section, "server_password")) password = config_get_string(global, section, "server_password");
                 
                 configFound = true;
             }
        }
    }
    
    result->Set(context, v8::String::NewFromUtf8(isolate, "enabled").ToLocalChecked(), v8::Boolean::New(isolate, enabled)).Check();
    
    if (portFound) {
        result->Set(context, v8::String::NewFromUtf8(isolate, "port").ToLocalChecked(), v8::Integer::New(isolate, port)).Check();
    }
    
    result->Set(context, v8::String::NewFromUtf8(isolate, "authRequired").ToLocalChecked(), v8::Boolean::New(isolate, auth)).Check();
    
    if (password) {
        result->Set(context, v8::String::NewFromUtf8(isolate, "password").ToLocalChecked(), v8::String::NewFromUtf8(isolate, password).ToLocalChecked()).Check();
    } else {
        result->Set(context, v8::String::NewFromUtf8(isolate, "password").ToLocalChecked(), v8::String::Empty(isolate)).Check();
    }
    
    args.GetReturnValue().Set(result);
}

static void WebSocketGetApiVersion(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    unsigned int version = obs_websocket_get_api_version();
    args.GetReturnValue().Set(v8::Integer::New(isolate, version));
}

void SetupWebSocketBindings(v8::Isolate* isolate, v8::Local<v8::Object> obs) {
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Object> websocket = v8::Object::New(isolate);

    websocket->Set(context, 
        v8::String::NewFromUtf8(isolate, "emit").ToLocalChecked(),
        v8::Function::New(context, WebSocketEmit).ToLocalChecked()
    ).Check();
    
    websocket->Set(context, 
        v8::String::NewFromUtf8(isolate, "on").ToLocalChecked(),
        v8::Function::New(context, WebSocketOn).ToLocalChecked()
    ).Check();
    
    websocket->Set(context, 
        v8::String::NewFromUtf8(isolate, "call").ToLocalChecked(),
        v8::Function::New(context, WebSocketCall).ToLocalChecked()
    ).Check();
    
    websocket->Set(context, 
        v8::String::NewFromUtf8(isolate, "getServerInfo").ToLocalChecked(),
        v8::Function::New(context, WebSocketGetServerInfo).ToLocalChecked()
    ).Check();

    websocket->Set(context, 
        v8::String::NewFromUtf8(isolate, "getApiVersion").ToLocalChecked(),
        v8::Function::New(context, WebSocketGetApiVersion).ToLocalChecked()
    ).Check();

    obs->Set(context,
        v8::String::NewFromUtf8(isolate, "websocket").ToLocalChecked(),
        websocket
    ).Check();
}
} // namespace obs_bindings
