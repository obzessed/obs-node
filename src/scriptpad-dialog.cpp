/**
 * scriptpad-dialog.cpp - Scriptpad/REPL dialog implementation
 */

#include "scriptpad-dialog.h"
#include "obs-node-runner.h"
#include "obs-node.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QMessageBox>
#include <QScrollBar>

#include <memory>

// Check if Node.js is initialized
namespace obs_node {
    extern bool is_initialized();
}

ScriptpadDialog::ScriptpadDialog(QWidget* parent)
    : QDialog(parent)
{
    setupUI();
    setupShortcuts();
    setWindowTitle("Node.js Scriptpad");
    setMinimumSize(700, 500);
    resize(900, 600);
}

ScriptpadDialog::~ScriptpadDialog() {
    // Ensure any running script is terminated
    if (currentRunner_ && currentRunner_->is_running()) {
        currentRunner_->terminate();
        currentRunner_->wait(std::chrono::seconds(2));
    }
}

void ScriptpadDialog::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(8);

    // Splitter for editor and output
    splitter_ = new QSplitter(Qt::Vertical, this);

    // Editor panel
    auto* editorPanel = new QWidget();
    auto* editorLayout = new QVBoxLayout(editorPanel);
    editorLayout->setContentsMargins(0, 0, 0, 0);
    editorLayout->setSpacing(4);

    auto* editorLabel = new QLabel("Script Editor (Ctrl+Enter to run):");
    editorWidget_ = new QPlainTextEdit();
    editorWidget_->setPlaceholderText(
        "// Write your JavaScript here\n"
        "// Access OBS API via __ctx or global 'obs'\n\n"
        "__ctx.log.info('Hello from Node.js!');\n"
        "console.log('Node version:', process.version);"
    );
    
    // Monospace font for editor
    QFont monoFont("Consolas", 10);
    monoFont.setStyleHint(QFont::Monospace);
    editorWidget_->setFont(monoFont);
    editorWidget_->setTabStopDistance(40);
    editorWidget_->setLineWrapMode(QPlainTextEdit::NoWrap);

    editorLayout->addWidget(editorLabel);
    editorLayout->addWidget(editorWidget_);

    // Output panel
    auto* outputPanel = new QWidget();
    auto* outputLayout = new QVBoxLayout(outputPanel);
    outputLayout->setContentsMargins(0, 0, 0, 0);
    outputLayout->setSpacing(4);

    auto* outputLabel = new QLabel("Output:");
    outputWidget_ = new QPlainTextEdit();
    outputWidget_->setReadOnly(true);
    outputWidget_->setFont(monoFont);
    outputWidget_->setLineWrapMode(QPlainTextEdit::WidgetWidth);

    outputLayout->addWidget(outputLabel);
    outputLayout->addWidget(outputWidget_);

    splitter_->addWidget(editorPanel);
    splitter_->addWidget(outputPanel);
    splitter_->setStretchFactor(0, 2);
    splitter_->setStretchFactor(1, 1);

    // Button bar
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(8);

    runButton_ = new QPushButton("▶ Run (Ctrl+Enter)");
    runButton_->setStyleSheet("QPushButton { background-color: #4CAF50; color: white; padding: 8px 16px; }");
    
    clearEditorButton_ = new QPushButton("Clear Editor");
    clearOutputButton_ = new QPushButton("Clear Output");
    
    statusLabel_ = new QLabel("Ready");
    statusLabel_->setStyleSheet("color: #666;");

    buttonLayout->addWidget(runButton_);
    buttonLayout->addWidget(clearEditorButton_);
    buttonLayout->addWidget(clearOutputButton_);
    buttonLayout->addStretch();
    buttonLayout->addWidget(statusLabel_);

    // Connect signals
    connect(runButton_, &QPushButton::clicked, this, &ScriptpadDialog::runScript);
    connect(clearEditorButton_, &QPushButton::clicked, this, &ScriptpadDialog::clearEditor);
    connect(clearOutputButton_, &QPushButton::clicked, this, &ScriptpadDialog::clearOutput);

    mainLayout->addWidget(splitter_);
    mainLayout->addLayout(buttonLayout);
}

void ScriptpadDialog::setupShortcuts()
{
    // Ctrl+Enter to run
    auto* runShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this);
    connect(runShortcut, &QShortcut::activated, this, &ScriptpadDialog::runScript);
    
    // Ctrl+Shift+Enter also
    auto* runShortcut2 = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Return), this);
    connect(runShortcut2, &QShortcut::activated, this, &ScriptpadDialog::runScript);
}

void ScriptpadDialog::runScript()
{
    // Check if Node.js is initialized
    if (!obs_node::is_initialized()) {
        appendOutput("Error: Node.js runtime not initialized.", true);
        statusLabel_->setText("Not initialized");
        statusLabel_->setStyleSheet("color: #F44336;");
        return;
    }

    // Check if already running
    if (currentRunner_ && currentRunner_->is_running()) {
        appendOutput("Script already running. Please wait or terminate.", true);
        return;
    }

    QString script = editorWidget_->toPlainText();
    if (script.trimmed().isEmpty()) {
        appendOutput("No script to run.", true);
        return;
    }

    statusLabel_->setText("Running...");
    statusLabel_->setStyleSheet("color: #2196F3;");
    runButton_->setEnabled(false);
    
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    appendOutput(QString("\n[%1] ▶ Running script...").arg(timestamp));

    try {
        // Create script runner (keep alive in member)
        currentRunner_ = std::make_unique<obs_node::ScriptRunner>();
        
        obs_node::ScriptRunner::Options options;
        options.timeout = std::chrono::seconds(30);
        options.spin_event_loop = true;
        
        options.on_complete = [this](bool success, const std::string& error) {
            // Marshal to UI thread
            QMetaObject::invokeMethod(this, [this, success, error] {
                runButton_->setEnabled(true);
                
                if (success) {
                    appendOutput("✓ Script completed successfully.");
                    statusLabel_->setText("Completed");
                    statusLabel_->setStyleSheet("color: #4CAF50;");
                } else {
                    QString errorMsg = QString::fromStdString(error);
                    if (errorMsg.isEmpty()) {
                        errorMsg = "Unknown error";
                    }
                    appendOutput(QString("✗ Error: %1").arg(errorMsg), true);
                    statusLabel_->setText("Error");
                    statusLabel_->setStyleSheet("color: #F44336;");
                }
            }, Qt::QueuedConnection);
        };

        // Run the script
        if (!currentRunner_->run(script.toStdString(), options)) {
            appendOutput("Failed to start script execution.", true);
            statusLabel_->setText("Failed");
            statusLabel_->setStyleSheet("color: #F44336;");
            runButton_->setEnabled(true);
            return;
        }

        // Don't wait synchronously - let it run async

    } catch (const std::exception& e) {
        appendOutput(QString("Exception: %1").arg(e.what()), true);
        statusLabel_->setText("Exception");
        statusLabel_->setStyleSheet("color: #F44336;");
        runButton_->setEnabled(true);
    } catch (...) {
        appendOutput("Unknown exception occurred.", true);
        statusLabel_->setText("Exception");
        statusLabel_->setStyleSheet("color: #F44336;");
        runButton_->setEnabled(true);
    }
}

void ScriptpadDialog::clearOutput()
{
    outputWidget_->clear();
    statusLabel_->setText("Ready");
    statusLabel_->setStyleSheet("color: #666;");
}

void ScriptpadDialog::clearEditor()
{
    editorWidget_->clear();
}

void ScriptpadDialog::appendOutput(const QString& text, bool isError)
{
    QString colored = text;
    if (isError) {
        colored = QString("<span style='color: #F44336;'>%1</span>").arg(text.toHtmlEscaped());
        outputWidget_->appendHtml(colored);
    } else {
        outputWidget_->appendPlainText(text);
    }
    
    // Scroll to bottom
    outputWidget_->verticalScrollBar()->setValue(
        outputWidget_->verticalScrollBar()->maximum()
    );
}
