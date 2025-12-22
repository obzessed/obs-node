/**
 * obs-node-ui.cpp - UI registration for OBS frontend
 * 
 * Registers menu items in the OBS Tools menu for the Node.js scriptpad,
 * and adds a REPL dock to the OBS docks menu.
 */

#include "obs-node-ui.h"
#include "obs-node.h"

// FIXME
#define OBS_UI_ENABLED_X 1
#define OBS_QT_ENABLED_X 1

// Only compile UI code if Qt and frontend API are available
#if defined(OBS_UI_ENABLED_X) && defined(OBS_QT_ENABLED_X)

#include "script-editor-dialog.h"
#include "repl-dock.h"

#include <obs-module.h>
#include <obs-frontend-api.h>

#include <QMainWindow>
#include <QAction>
#include <QMenu>

// Global instances (lazy creation)
static ScriptEditorDialog* g_scriptpadDialog = nullptr;

/**
 * Show the scriptpad dialog
 */
static void show_scriptpad_dialog()
{
    if (!g_scriptpadDialog) {
        auto* mainWindow = static_cast<QMainWindow*>(
            obs_frontend_get_main_window()
        );
        g_scriptpadDialog = new ScriptEditorDialog(mainWindow);
    }
    
    g_scriptpadDialog->show();
    g_scriptpadDialog->raise();
    g_scriptpadDialog->activateWindow();
}

/**
 * Frontend event callback
 */
static void frontend_event_callback(obs_frontend_event event, void* data)
{
    (void)data;
    
    if (event == OBS_FRONTEND_EVENT_EXIT) {
        if (g_scriptpadDialog) {
            delete g_scriptpadDialog;
            g_scriptpadDialog = nullptr;
        }
    }
}

extern "C" void obs_node_register_ui(void)
{
    // Register frontend event handler
    obs_frontend_add_event_callback(frontend_event_callback, nullptr);
    
    // Add menu item to Tools menu
    auto* action = static_cast<QAction*>(
        obs_frontend_add_tools_menu_qaction("Node Script Editor")
    );
    
    if (action) {
        QObject::connect(action, &QAction::triggered, [] {
            show_scriptpad_dialog();
        });
        obs_log(LOG_INFO, "Registered 'Node Script Editor' in Tools menu");
    }

    // Add REPL dock - pass widget directly, OBS creates the dock widget
    auto* replWidget = new ReplDock();
    replWidget->setMinimumSize(300, 200);
    obs_frontend_add_dock_by_id("NodeReplDock", "Node REPL", replWidget);
    obs_log(LOG_INFO, "Registered 'Node REPL' dock");
}

extern "C" void obs_node_unregister_ui(void)
{
    obs_frontend_remove_event_callback(frontend_event_callback, nullptr);
    
    if (g_scriptpadDialog) {
        delete g_scriptpadDialog;
        g_scriptpadDialog = nullptr;
    }
}

#else

// Stub implementations when Qt/frontend API not available
#include <obs-module.h>

extern "C" void obs_node_register_ui(void)
{
    obs_log(LOG_INFO, "UI disabled (Qt/frontend API not enabled)");
}

extern "C" void obs_node_unregister_ui(void)
{
    // Nothing to unregister
}

#endif // ENABLE_FRONTEND_API && ENABLE_QT
