/**
 * scriptpad-dialog.h - Scriptpad/REPL dialog for OBS
 * 
 * Provides a simple script editor and REPL interface for running
 * JavaScript code in the OBS Node.js environment.
 */

#pragma once

#include <QDialog>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QStatusBar>
#include <QShortcut>
#include <QFont>
#include <QTimer>

#include <memory>

// Forward declaration
namespace obs_node { class ScriptRunner; }

class ScriptpadDialog : public QDialog {
    Q_OBJECT

public:
    explicit ScriptpadDialog(QWidget* parent = nullptr);
    ~ScriptpadDialog() override;

public slots:
    void runScript();
    void clearOutput();
    void clearEditor();

private:
    void setupUI();
    void setupShortcuts();
    void appendOutput(const QString& text, bool isError = false);

    // UI Components
    QPlainTextEdit* editorWidget_ = nullptr;
    QPlainTextEdit* outputWidget_ = nullptr;
    QPushButton* runButton_ = nullptr;
    QPushButton* clearOutputButton_ = nullptr;
    QPushButton* clearEditorButton_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QSplitter* splitter_ = nullptr;
    
    // Script runner (kept alive for async execution)
    std::unique_ptr<obs_node::ScriptRunner> currentRunner_;
};
