/**
 * obs-hotkeys.cpp - OBS hotkey bindings
 * 
 * Exposes obs.hotkeys API for registering and handling global hotkeys.
 */

#include "obs-bindings.h"
#include <obs.h>
#include <obs-frontend-api.h>
#include <util/platform.h>
#include <string>
#include <vector>
#include <mutex>

namespace obs_bindings {

struct HotkeyCallback {
    v8::Isolate* isolate;
    v8::Global<v8::Function> callback;
    v8::Global<v8::Context> context;
    obs_hotkey_id id = OBS_INVALID_HOTKEY_ID;
};

// Store allocated callbacks to free them later
static std::mutex g_hotkeyMutex;
static std::vector<HotkeyCallback*> g_hotkeyCallbacks;

static void OnHotkeyTrigger(void* data, obs_hotkey_id id, obs_hotkey_t* hotkey, bool pressed) {
    if (!pressed) return; // Only trigger on press

    HotkeyCallback* cb = static_cast<HotkeyCallback*>(data);
    if (!cb || !cb->isolate || cb->callback.IsEmpty()) return;

    // Thread safety: Lock V8 to execute on this thread (OBS main thread)
    v8::Locker locker(cb->isolate);
    v8::Isolate::Scope isolate_scope(cb->isolate);
    v8::HandleScope handle_scope(cb->isolate);
    
    v8::Local<v8::Context> context = cb->context.Get(cb->isolate);
    v8::Context::Scope context_scope(context);

    v8::Local<v8::Function> fn = cb->callback.Get(cb->isolate);

    // Pass 'pressed' state if we ever enable release triggers, 
    // but for now hotkeys are just "actions".
    // We could pass event object: { pressed: true, id: 123 }
    v8::Local<v8::Object> event = v8::Object::New(cb->isolate);
    event->Set(context, 
        v8::String::NewFromUtf8(cb->isolate, "pressed").ToLocalChecked(),
        v8::Boolean::New(cb->isolate, pressed)
    ).Check();

    v8::Local<v8::Value> argv[] = { event };

    v8::TryCatch try_catch(cb->isolate);
    fn->Call(context, context->Global(), 1, argv);

    if (try_catch.HasCaught()) {
        v8::String::Utf8Value err(cb->isolate, try_catch.Exception());
        // LOG_ERROR("Msg", *err); // We need access to logging or just ignore
    }
}

// obs.hotkeys.register(name, description, callback) -> id
static void HotkeysRegister(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();

    if (args.Length() < 3 || !args[0]->IsString() || !args[1]->IsString() || !args[2]->IsFunction()) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8(isolate, "Expected (name, description, callback)").ToLocalChecked()));
        return;
    }

    v8::String::Utf8Value name(isolate, args[0]);
    v8::String::Utf8Value desc(isolate, args[1]);
    v8::Local<v8::Function> func = args[2].As<v8::Function>();

    HotkeyCallback* cb = new HotkeyCallback();
    cb->isolate = isolate;
    cb->callback.Reset(isolate, func);
    cb->context.Reset(isolate, context);

    {
        std::lock_guard<std::mutex> lock(g_hotkeyMutex);
        g_hotkeyCallbacks.push_back(cb);
    }

    // Register with OBS
    cb->id = obs_hotkey_register_frontend(*name, *desc, OnHotkeyTrigger, cb);

    args.GetReturnValue().Set(v8::Integer::New(isolate, (int32_t)cb->id));
}

// obs.hotkeys.unregister(id)
static void HotkeysUnregister(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    
    if (args.Length() < 1 || !args[0]->IsNumber()) {
        return;
    }

    obs_hotkey_id id = (obs_hotkey_id)args[0]->Int32Value(isolate->GetCurrentContext()).FromJust();
    obs_hotkey_unregister(id);
    
    // Cleanup callback logic is tricky because we can't easily find ownership 
    // without scanning vector, but 'obs_hotkey_unregister' doesn't free the user data.
    // We should clean up `g_hotkeyCallbacks` but we need to match the ID.
    
    std::lock_guard<std::mutex> lock(g_hotkeyMutex);
    for (auto it = g_hotkeyCallbacks.begin(); it != g_hotkeyCallbacks.end(); ++it) {
        if ((*it)->id == id) {
            HotkeyCallback* cb = *it;
            cb->callback.Reset();
            cb->context.Reset();
            delete cb;
            g_hotkeyCallbacks.erase(it);
            break;
        }
    }
}

// obs.hotkeys.trigger(id, pressed) - Debug/Manual trigger
static void HotkeysTrigger(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    
    if (args.Length() < 2 || !args[0]->IsString() || !args[1]->IsBoolean()) return;
    
    // We can't easily trigger generic hotkeys by ID via API 
    // effectively without knowing the hotkey object pointer? 
    // `obs_hotkey_trigger_routed_callback` needs `obs_hotkey_t*`.
    // But `obs_get_hotkey_by_id` exists?
    // Not exposed in standard headers easily? 
    // Let's skip direct triggering by ID if complex.
    // We can allow triggering by Name via `obs_frontend_event` workaround? No.
    
    // Actually, `obs_hotkey_trigger_control` maybe?
    // Let's skip 'trigger' for now, register/unregister is core.
}

void SetupHotkeyBindings(v8::Isolate* isolate, v8::Local<v8::Object> obs) {
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    v8::Local<v8::Object> hotkeys = v8::Object::New(isolate);
    
    auto setFunc = [&](const char* name, v8::FunctionCallback cb) {
        v8::Local<v8::Function> fn = v8::Function::New(context, cb).ToLocalChecked();
        hotkeys->Set(context, 
            v8::String::NewFromUtf8(isolate, name).ToLocalChecked(),
            fn
        ).Check();
    };
    
    setFunc("register", HotkeysRegister);
    setFunc("unregister", HotkeysUnregister);
    
    obs->Set(context,
        v8::String::NewFromUtf8(isolate, "hotkeys").ToLocalChecked(),
        hotkeys
    ).Check();
}

} // namespace obs_bindings
