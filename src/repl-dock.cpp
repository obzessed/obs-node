/**
 * repl-dock.cpp - REPL dock widget implementation
 */

#include "repl-dock.h"
#include "obs-node-runner.h"
#include "obs-node.h"

#include <QScrollBar>
#include <QShortcut>

namespace obs_node {
    extern bool is_initialized();
}

ReplDock::ReplDock(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
}

ReplDock::~ReplDock() = default;

void ReplDock::setupUI()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    // Output area
    outputWidget_ = new QPlainTextEdit();
    outputWidget_->setReadOnly(true);
    outputWidget_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    
    QFont monoFont("Consolas", 9);
    monoFont.setStyleHint(QFont::Monospace);
    outputWidget_->setFont(monoFont);
    outputWidget_->setStyleSheet(
        "QPlainTextEdit { background-color: #1e1e1e; color: #d4d4d4; }"
    );

    // Input line
    inputWidget_ = new QLineEdit();
    inputWidget_->setFont(monoFont);
    inputWidget_->setPlaceholderText("> Type JavaScript and press Enter");
    inputWidget_->setStyleSheet(
        "QLineEdit { background-color: #252526; color: #d4d4d4; "
        "border: 1px solid #3c3c3c; padding: 4px; }"
    );
    inputWidget_->installEventFilter(this);

    connect(inputWidget_, &QLineEdit::returnPressed, this, &ReplDock::executeCommand);

    // Ctrl+L to clear screen
    auto* clearShortcut = new QShortcut(QKeySequence("Ctrl+L"), inputWidget_);
    connect(clearShortcut, &QShortcut::activated, this, &ReplDock::clearOutput);

    layout->addWidget(outputWidget_, 1);
    layout->addWidget(inputWidget_);

    // Welcome message
    appendOutput("Node.js REPL ready. Ctrl+L to clear.\n");
}

bool ReplDock::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == inputWidget_ && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Up) {
            historyUp();
            return true;
        } else if (keyEvent->key() == Qt::Key_Down) {
            historyDown();
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

void ReplDock::executeCommand()
{
    QString command = inputWidget_->text().trimmed();
    if (command.isEmpty()) return;

    // Add to history
    commandHistory_.prepend(command);
    if (commandHistory_.size() > 100) {
        commandHistory_.removeLast();
    }
    historyIndex_ = -1;

    inputWidget_->clear();

    // Show command
    appendOutput("> " + command);

    if (!obs_node::is_initialized()) {
        appendOutput("Error: Node.js not initialized", true);
        return;
    }

    // Execute using ScriptRunner
    obs_node::ScriptRunner runner;
    obs_node::ScriptRunner::Options options;
    options.timeout = std::chrono::seconds(5);
    options.spin_event_loop = false;

    // Wrap to capture result and store in global._
    QString wrappedScript = QString(
        "(function() {"
        "  var __r = eval(\"%1\");"
        "  globalThis._ = __r;"
        "  if (__r === undefined) return '';"
        "  if (typeof __r === 'string') return __r;"
        "  try { return JSON.stringify(__r); } catch(e) { return String(__r); }"
        "})()"
    ).arg(command.replace("\\", "\\\\").replace("\"", "\\\"").replace("\n", "\\n"));

    if (!runner.run(wrappedScript.toStdString(), options)) {
        appendOutput("Failed to run command", true);
        return;
    }

    runner.wait(std::chrono::seconds(6));

    if (runner.succeeded()) {
        QString result = QString::fromStdString(runner.get_result());
        if (!result.isEmpty()) {
            appendResult(result);
        }
    } else {
        appendOutput(QString::fromStdString(runner.get_error()), true);
    }
}

void ReplDock::clearOutput()
{
    outputWidget_->clear();
}

void ReplDock::appendOutput(const QString& text, bool isError)
{
    if (isError) {
        outputWidget_->appendHtml(
            QString("<span style='color: #f44336;'>%1</span>").arg(text.toHtmlEscaped())
        );
    } else {
        outputWidget_->appendPlainText(text);
    }
    outputWidget_->verticalScrollBar()->setValue(
        outputWidget_->verticalScrollBar()->maximum()
    );
}

void ReplDock::appendResult(const QString& text)
{
    outputWidget_->appendHtml(
        QString("<span style='color: #4ec9b0;'>%1</span>").arg(text.toHtmlEscaped())
    );
    outputWidget_->verticalScrollBar()->setValue(
        outputWidget_->verticalScrollBar()->maximum()
    );
}

void ReplDock::historyUp()
{
    if (commandHistory_.isEmpty()) return;
    if (historyIndex_ < commandHistory_.size() - 1) {
        historyIndex_++;
        inputWidget_->setText(commandHistory_[historyIndex_]);
    }
}

void ReplDock::historyDown()
{
    if (historyIndex_ > 0) {
        historyIndex_--;
        inputWidget_->setText(commandHistory_[historyIndex_]);
    } else if (historyIndex_ == 0) {
        historyIndex_ = -1;
        inputWidget_->clear();
    }
}
