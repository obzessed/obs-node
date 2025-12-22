/**
 * repl-dock.cpp - REPL dock widget implementation
 */

#include "repl-dock.h"
#include "obs-node-runner.h"
#include "obs-node.h"

#include <obs.h>
#include <obs-module.h>
#include <util/platform.h>
#include <node/node_version.h>
#include <QScrollBar>
#include <QShortcut>
#include <QDockWidget>
#include <QRegularExpression>

namespace obs_node {
    extern bool is_initialized();
}

// Convert ANSI escape codes to HTML spans with colors
static QString ansiToHtml(const QString& input) {
    // ANSI color code to HTML color mapping (Node.js inspect colors)
    static const QMap<int, QString> colorMap = {
        {0, ""},           // Reset
        {1, "font-weight: bold"},  // Bold
        {2, "opacity: 0.7"},       // Dim
        {22, "font-weight: normal"}, // Normal intensity
        {30, "color: #1e1e1e"},    // Black
        {31, "color: #f44336"},    // Red
        {32, "color: #4caf50"},    // Green
        {33, "color: #ffeb3b"},    // Yellow
        {34, "color: #2196f3"},    // Blue
        {35, "color: #e91e63"},    // Magenta
        {36, "color: #00bcd4"},    // Cyan
        {37, "color: #d4d4d4"},    // White
        {39, "color: #d4d4d4"},    // Default
        {90, "color: #808080"},    // Bright black (gray)
        {91, "color: #ff5252"},    // Bright red
        {92, "color: #69f0ae"},    // Bright green
        {93, "color: #ffff00"},    // Bright yellow
        {94, "color: #448aff"},    // Bright blue
        {95, "color: #ff4081"},    // Bright magenta
        {96, "color: #18ffff"},    // Bright cyan
        {97, "color: #ffffff"},    // Bright white
    };
    
    QString result;
    result.reserve(input.size() * 2);
    
    // Match ANSI escape sequences: ESC[...m
    static QRegularExpression ansiRegex(R"(\x1b\[([0-9;]*)m)");
    
    int lastEnd = 0;
    bool inSpan = false;
    QRegularExpressionMatchIterator it = ansiRegex.globalMatch(input);
    
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        
        // Append text before this match (HTML escaped)
        QString textBefore = input.mid(lastEnd, match.capturedStart() - lastEnd);
        result += textBefore.toHtmlEscaped().replace("\n", "<br>").replace(" ", "&nbsp;");
        
        // Parse the ANSI codes
        QString codes = match.captured(1);
        if (codes.isEmpty() || codes == "0") {
            // Reset
            if (inSpan) {
                result += "</span>";
                inSpan = false;
            }
        } else {
            // Close previous span if open
            if (inSpan) {
                result += "</span>";
            }
            
            // Build style from codes
            QStringList styles;
            for (const QString& codeStr : codes.split(';')) {
                int code = codeStr.toInt();
                if (colorMap.contains(code) && !colorMap[code].isEmpty()) {
                    styles << colorMap[code];
                }
            }
            
            if (!styles.isEmpty()) {
                result += QString("<span style='%1'>").arg(styles.join("; "));
                inSpan = true;
            }
        }
        
        lastEnd = match.capturedEnd();
    }
    
    // Append remaining text
    QString remaining = input.mid(lastEnd);
    result += remaining.toHtmlEscaped().replace("\n", "<br>").replace(" ", "&nbsp;");
    
    // Close any open span
    if (inSpan) {
        result += "</span>";
    }
    
    return result;
}

ReplDock::ReplDock(QWidget* parent)
    : QWidget(parent)
{
    loadHistory();
    setupUI();
    
    // Register console callback
    obs_node::SetConsoleCallback([this](const std::string& type, const std::string& msg) {
        QMetaObject::invokeMethod(this, [this, type, msg]() {
            // Log/Info = default color (cyan/green from inspect)
            // Warn = Yellow
            // Error = Red
            // Node.js inspect already colors the message, so we just wrap it if needed.
            // But if it's a raw string log, we might want to color the prefix.
            
            QString content = ansiToHtml(QString::fromStdString(msg));
            if (content.isEmpty()) content = "<span style='color: #808080'>undefined</span>";
            
            outputWidget_->appendHtml(content);
            outputWidget_->verticalScrollBar()->setValue(
                outputWidget_->verticalScrollBar()->maximum()
            );
        }, Qt::QueuedConnection);
    });
}

ReplDock::~ReplDock() {
    saveHistory();
}

void ReplDock::setupUI()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    // Output area
    outputWidget_ = new QPlainTextEdit();
    outputWidget_->setReadOnly(true);
    outputWidget_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    
    QFont monoFont("JetBrains Mono");
    monoFont.setStyleHint(QFont::Monospace);
    if (monoFont.exactMatch()) {
        monoFont.setPointSize(10);
    } else {
        monoFont = QFont("Consolas", 10);
        monoFont.setStyleHint(QFont::Monospace);
    }
    
    outputWidget_->setFont(monoFont);
    outputWidget_->setStyleSheet(
        "QPlainTextEdit { "
        "   background-color: #1e1e1e; "
        "   color: #d4d4d4; "
        "   font-family: 'JetBrains Mono', 'Consolas', 'Courier New', monospace; "
        "   font-size: 10pt; "
        "}"
    );

    // Input line
    inputWidget_ = new QLineEdit();
    inputWidget_->setFont(monoFont);
    inputWidget_->setPlaceholderText("> Type JavaScript and press Enter");
    inputWidget_->setStyleSheet(
        "QLineEdit { "
        "   background-color: #252526; "
        "   color: #d4d4d4; "
        "   border: 1px solid #3c3c3c; "
        "   padding: 4px; "
        "   font-family: 'JetBrains Mono', 'Consolas', 'Courier New', monospace; "
        "   font-size: 10pt; "
        "}"
    );
    inputWidget_->installEventFilter(this);

    connect(inputWidget_, &QLineEdit::returnPressed, this, &ReplDock::executeCommand);

    // Ctrl+L to clear screen (only when input focused)
    auto* clearShortcut = new QShortcut(QKeySequence("Ctrl+L"), inputWidget_);
    clearShortcut->setContext(Qt::WidgetShortcut);
    connect(clearShortcut, &QShortcut::activated, this, &ReplDock::clearOutput);

    layout->addWidget(outputWidget_, 1);
    layout->addWidget(inputWidget_);

    // Welcome message (Node.js REPL style)
    appendOutput(QString("Welcome to Node.js v%1 (OBS %2).")
        .arg(QString::fromUtf8(NODE_VERSION_STRING))
        .arg(QString::fromUtf8(obs_get_version_string())));
    appendOutput("Type .help for more information.");
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
    saveHistory(); // Auto-save history on change for crash resilience

    inputWidget_->clear();

    // Show command
    appendOutput("> " + command);

    // Handle dot commands (like Node.js REPL)
    if (command.startsWith(".")) {
        QString cmd = command.toLower();
        if (cmd == ".help") {
            appendOutput(".clear    Clear the output");
            appendOutput(".exit     Close the REPL dock");
            appendOutput(".help     Show this help");
            appendOutput(".history  Show command history");
            appendOutput(".version  Show version info");
            appendOutput("");
            appendOutput("Press Ctrl+L to clear, Up/Down for history");
            return;
        } else if (cmd == ".clear") {
            clearOutput();
            return;
        } else if (cmd == ".exit") {
            if (auto* dock = qobject_cast<QDockWidget*>(parentWidget())) {
                dock->close();
            }
            return;
        } else if (cmd == ".history") {
            if (commandHistory_.isEmpty()) {
                appendOutput("(no history)");
            } else {
                for (int i = commandHistory_.size() - 1; i >= 0; i--) {
                    appendOutput(QString("%1: %2").arg(i).arg(commandHistory_[i]));
                }
            }
            return;
        } else if (cmd == ".version") {
            appendOutput(QString("Node.js v%1").arg(QString::fromUtf8(NODE_VERSION_STRING)));
            appendOutput(QString("OBS %1").arg(QString::fromUtf8(obs_get_version_string())));
            return;
        } else {
            appendOutput(QString("Invalid REPL command '%1'. Type .help for options.").arg(command), true);
            return;
        }
    }

    if (!obs_node::is_initialized()) {
        appendOutput("Error: Node.js not initialized", true);
        return;
    }

    // Execute using ScriptRunner
    obs_node::ScriptRunner runner;
    obs_node::ScriptRunner::Options options;
    options.timeout = std::chrono::seconds(5);
    options.spin_event_loop = false;

    // Wrap to capture result and store in global._ with Node.js REPL-style output
    QString wrappedScript = QString(
        "(function() {"
        "  var __r = eval(\"%1\");"
        "  globalThis._ = __r;"
        "  if (__r === undefined) return '';"
        "  try {"
        "    var util = globalThis.require ? globalThis.require('util') : null;"
        "    if (util && util.inspect) return util.inspect(__r, { colors: true, depth: 4, maxArrayLength: 100, breakLength: 60, compact: false });"
        "  } catch(e) {}"
        "  if (typeof __r === 'string') return __r;"
        "  try { return JSON.stringify(__r, null, 2); } catch(e) { return String(__r); }"
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
    // Convert ANSI color codes to HTML for colorful output
    QString html = ansiToHtml(text);
    outputWidget_->appendHtml(html);
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

void ReplDock::loadHistory()
{
    char* path = obs_module_config_path("repl_history.json");
    if (!path) return;
    
    obs_data_t* data = obs_data_create_from_json_file(path);
    bfree(path);
    
    if (!data) return;
    
    obs_data_array_t* arr = obs_data_get_array(data, "history");
    if (arr) {
        size_t count = obs_data_array_count(arr);
        for (size_t i = 0; i < count && i < 100; i++) {
            obs_data_t* item = obs_data_array_item(arr, i);
            const char* cmd = obs_data_get_string(item, "cmd");
            if (cmd && *cmd) {
                commandHistory_.append(QString::fromUtf8(cmd));
            }
            obs_data_release(item);
        }
        obs_data_array_release(arr);
    }
    
    obs_data_release(data);
}

void ReplDock::saveHistory()
{
    char* path = obs_module_config_path("repl_history.json");
    if (!path) return;
    
    // Ensure directory exists
    char* dir = obs_module_config_path("");
    if (dir) {
        os_mkdirs(dir);
        bfree(dir);
    }
    
    obs_data_t* data = obs_data_create();
    obs_data_array_t* arr = obs_data_array_create();
    
    int count = qMin(commandHistory_.size(), 100);
    for (int i = 0; i < count; i++) {
        obs_data_t* item = obs_data_create();
        obs_data_set_string(item, "cmd", commandHistory_[i].toUtf8().constData());
        obs_data_array_push_back(arr, item);
        obs_data_release(item);
    }
    
    obs_data_set_array(data, "history", arr);
    obs_data_save_json(data, path);
    
    obs_data_array_release(arr);
    obs_data_release(data);
    bfree(path);
}
