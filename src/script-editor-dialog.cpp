/**
 * script-editor-dialog.cpp - Node Script Editor implementation
 * 
 * Multi-script editor with persistence.
 */

#include "script-editor-dialog.h"
#include "obs-node-runner.h"
#include "obs-node.h"

#include <obs-frontend-api.h>

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QScrollBar>
#include <QDir>
#include <QCloseEvent>
#include <QPalette>
#include <QFileDialog>

#include <memory>

// Check if Node.js is initialized
namespace obs_node {
    extern bool is_initialized();
}

// ============================================================================
// CodeEditor Implementation - QPlainTextEdit with line numbers
// ============================================================================

CodeEditor::CodeEditor(QWidget* parent) : QPlainTextEdit(parent) {
    lineNumberArea_ = new LineNumberArea(this);

    connect(this, &CodeEditor::blockCountChanged, this, &CodeEditor::updateLineNumberAreaWidth);
    connect(this, &CodeEditor::updateRequest, this, &CodeEditor::updateLineNumberArea);
    connect(this, &CodeEditor::cursorPositionChanged, this, &CodeEditor::highlightCurrentLine);

    updateLineNumberAreaWidth(0);
    highlightCurrentLine();
}

int CodeEditor::lineNumberAreaWidth() {
    int digits = 1;
    int max = qMax(1, blockCount());
    while (max >= 10) {
        max /= 10;
        ++digits;
    }
    int space = 12 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
    return space;
}

void CodeEditor::updateLineNumberAreaWidth(int /* newBlockCount */) {
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void CodeEditor::updateLineNumberArea(const QRect& rect, int dy) {
    if (dy)
        lineNumberArea_->scroll(0, dy);
    else
        lineNumberArea_->update(0, rect.y(), lineNumberArea_->width(), rect.height());

    if (rect.contains(viewport()->rect()))
        updateLineNumberAreaWidth(0);
}

void CodeEditor::resizeEvent(QResizeEvent* e) {
    QPlainTextEdit::resizeEvent(e);

    QRect cr = contentsRect();
    lineNumberArea_->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

void CodeEditor::highlightCurrentLine() {
    QList<QTextEdit::ExtraSelection> extraSelections;

    // Only highlight if not read-only and document has content
    // (skip when empty to allow placeholder text to show)
    if (!isReadOnly() && !document()->isEmpty()) {
        QTextEdit::ExtraSelection selection;
        QColor lineColor = QColor(40, 40, 40);  // Dark highlight for current line
        selection.format.setBackground(lineColor);
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = textCursor();
        selection.cursor.clearSelection();
        extraSelections.append(selection);
    }

    setExtraSelections(extraSelections);
}

void CodeEditor::lineNumberAreaPaintEvent(QPaintEvent* event) {
    QPainter painter(lineNumberArea_);
    painter.fillRect(event->rect(), QColor(30, 30, 30));  // Dark background for line numbers
    
    // Set the painter font to match the editor font size
    QFont lineFont("JetBrains Mono", fontSize_);
    lineFont.setStyleHint(QFont::Monospace);
    painter.setFont(lineFont);

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());

    QFontMetrics fm = painter.fontMetrics();
    
    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            QString number = QString::number(blockNumber + 1);
            painter.setPen(QColor(100, 100, 100));  // Gray line numbers
            painter.drawText(0, top, lineNumberArea_->width() - 4, fm.height(),
                           Qt::AlignRight, number);
        }

        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNumber;
    }
}

void CodeEditor::zoomIn(int range) {
    setFontSize(fontSize_ + range);
}

void CodeEditor::zoomOut(int range) {
    setFontSize(fontSize_ - range);
}

void CodeEditor::setFontSize(int size) {
    // Clamp font size between 6 and 32
    fontSize_ = qBound(6, size, 32);
    
    // Update stylesheet with new font size
    setStyleSheet(
        QString(
            "QPlainTextEdit {"
            "  font-family: 'JetBrains Mono', 'Cascadia Code', 'Fira Code', Consolas, monospace;"
            "  font-size: %1pt;"
            "  background-color: #1e1e1e;"
            "  color: #d4d4d4;"
            "  border: 1px solid #3c3c3c;"
            "  border-radius: 4px;"
            "  padding: 8px;"
            "  selection-background-color: #264f78;"
            "}"
        ).arg(fontSize_)
    );
    
    // Update line number area font to match
    QFont lineFont("JetBrains Mono", fontSize_);
    lineFont.setStyleHint(QFont::Monospace);
    lineNumberArea_->setFont(lineFont);
    
    updateLineNumberAreaWidth(0);
}

void CodeEditor::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        // Ctrl+Scroll = zoom
        int delta = event->angleDelta().y();
        if (delta > 0) {
            zoomIn();
        } else if (delta < 0) {
            zoomOut();
        }
        event->accept();
        return;
    }
    QPlainTextEdit::wheelEvent(event);
}

// ============================================================================
// ScriptEditorDialog Implementation
// ============================================================================

ScriptEditorDialog::ScriptEditorDialog(QWidget* parent)
    : QDialog(parent)
{
    setupUI();
    setupShortcuts();
    setWindowTitle("Node Script Editor");
    setMinimumSize(900, 600);
    resize(1100, 700);
    
    // Load saved scripts
    loadScripts();
    
    // Select first script if available
    if (scriptListWidget_->count() > 0) {
        scriptListWidget_->setCurrentRow(0);
    }
}

ScriptEditorDialog::~ScriptEditorDialog() {
    // Ensure any running script is terminated
    if (currentRunner_ && currentRunner_->is_running()) {
        currentRunner_->terminate();
        currentRunner_->wait(std::chrono::seconds(2));
    }
}

void ScriptEditorDialog::closeEvent(QCloseEvent* event) {
    saveCurrentScript();
    saveScripts();
    QDialog::closeEvent(event);
}

bool ScriptEditorDialog::eventFilter(QObject* obj, QEvent* event) {
    // Handle Ctrl+Scroll zoom on output widget's viewport
    if (obj == outputWidget_->viewport() && event->type() == QEvent::Wheel) {
        auto* wheelEvent = static_cast<QWheelEvent*>(event);
        if (wheelEvent->modifiers() & Qt::ControlModifier) {
            // Ctrl+Scroll = zoom output
            int delta = wheelEvent->angleDelta().y();
            if (delta > 0) {
                outputFontSize_ = qBound(6, outputFontSize_ + 1, 32);
            } else if (delta < 0) {
                outputFontSize_ = qBound(6, outputFontSize_ - 1, 32);
            }
            
            // Update output widget stylesheet with new font size
            outputWidget_->setStyleSheet(
                QString(
                    "QPlainTextEdit {"
                    "  font-family: 'JetBrains Mono', 'Cascadia Code', 'Fira Code', Consolas, monospace;"
                    "  font-size: %1pt;"
                    "  background-color: #1a1a1a;"
                    "  color: #cccccc;"
                    "  border: 1px solid #3c3c3c;"
                    "  border-radius: 4px;"
                    "  padding: 8px;"
                    "}"
                ).arg(outputFontSize_)
            );
            
            return true;  // Event handled
        }
    }
    return QDialog::eventFilter(obj, event);
}

QString ScriptEditorDialog::getScriptsFilePath() {
    char* profilePath = obs_frontend_get_current_profile_path();
    QString path = QString::fromUtf8(profilePath) + "/node-scripts.json";
    bfree(profilePath);
    return path;
}

void ScriptEditorDialog::loadScripts() {
    scripts_.clear();
    
    QFile file(getScriptsFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        // Create default script
        scripts_["Untitled"] = "// Write your JavaScript here\n"
                               "// Access OBS API via 'obs' global\n\n"
                               "console.log('Node version:', process.version);";
    } else {
        QByteArray data = file.readAll();
        file.close();
        
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isObject()) {
            QJsonObject root = doc.object();
            QJsonObject scriptsObj = root["scripts"].toObject();
            
            for (auto it = scriptsObj.begin(); it != scriptsObj.end(); ++it) {
                scripts_[it.key()] = it.value().toString();
            }
            
            currentScriptName_ = root["lastOpen"].toString();
        }
    }
    
    // If no scripts, create default
    if (scripts_.isEmpty()) {
        scripts_["Untitled"] = "// New script";
    }
    
    // Populate list
    scriptListWidget_->clear();
    for (auto it = scripts_.begin(); it != scripts_.end(); ++it) {
        scriptListWidget_->addItem(it.key());
    }
    
    // Select last open or first
    if (!currentScriptName_.isEmpty()) {
        auto items = scriptListWidget_->findItems(currentScriptName_, Qt::MatchExactly);
        if (!items.isEmpty()) {
            scriptListWidget_->setCurrentItem(items.first());
        } else if (scriptListWidget_->count() > 0) {
            scriptListWidget_->setCurrentRow(0);
        }
    }
}

void ScriptEditorDialog::saveScripts() {
    QJsonObject scriptsObj;
    for (auto it = scripts_.begin(); it != scripts_.end(); ++it) {
        scriptsObj[it.key()] = it.value();
    }
    
    QJsonObject root;
    root["scripts"] = scriptsObj;
    root["lastOpen"] = currentScriptName_;
    
    QJsonDocument doc(root);
    
    QFile file(getScriptsFilePath());
    if (file.open(QIODevice::WriteOnly)) {
        file.write(doc.toJson());
        file.close();
    }
}

void ScriptEditorDialog::saveCurrentScript() {
    if (!currentScriptName_.isEmpty()) {
        scripts_[currentScriptName_] = editorWidget_->toPlainText();
    }
}

void ScriptEditorDialog::switchToScript(const QString& name) {
    if (name == currentScriptName_) return;
    
    // Save current before switching
    saveCurrentScript();
    
    currentScriptName_ = name;
    if (scripts_.contains(name)) {
        editorWidget_->setPlainText(scripts_[name]);
    } else {
        editorWidget_->clear();
    }
}

void ScriptEditorDialog::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(8);

    // Main horizontal splitter: script list | editor/output
    mainSplitter_ = new QSplitter(Qt::Horizontal, this);

    // =========================================================================
    auto* leftPanel = new QWidget();
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(4);

    // Script header (just label)
    auto* scriptsLabel = new QLabel("Scripts");
    scriptsLabel->setStyleSheet("font-weight: bold;");

    // Script list with context menu
    scriptListWidget_ = new QListWidget();
    scriptListWidget_->setMinimumWidth(150);
    scriptListWidget_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(scriptListWidget_, &QWidget::customContextMenuRequested, [this](const QPoint& pos) {
        QListWidgetItem* item = scriptListWidget_->itemAt(pos);
        if (!item) return;
        
        QMenu contextMenu(scriptListWidget_);
        
        QAction* renameAction = contextMenu.addAction("✎ Rename\tF2");
        connect(renameAction, &QAction::triggered, this, &ScriptEditorDialog::renameScript);
        
        QAction* duplicateAction = contextMenu.addAction("⧉ Duplicate\tCtrl+D");
        connect(duplicateAction, &QAction::triggered, this, &ScriptEditorDialog::duplicateScript);
        
        contextMenu.addSeparator();
        
        QAction* exportAction = contextMenu.addAction("↗ Export...\tCtrl+E");
        connect(exportAction, &QAction::triggered, this, &ScriptEditorDialog::exportScript);
        
        contextMenu.addSeparator();
        
        QAction* deleteAction = contextMenu.addAction("× Delete\tDel");
        deleteAction->setProperty("destructive", true);
        connect(deleteAction, &QAction::triggered, this, &ScriptEditorDialog::deleteScript);
        
        contextMenu.exec(scriptListWidget_->mapToGlobal(pos));
    });

    // Bottom button row (New + Import)
    auto* bottomButtonLayout = new QHBoxLayout();
    bottomButtonLayout->setSpacing(4);
    
    newButton_ = new QPushButton("+ New");
    newButton_->setToolTip("New Script (Ctrl+N)");
    newButton_->setStyleSheet(
        "QPushButton { padding: 6px; font-weight: bold; }"
        "QPushButton:hover { background-color: #3c3c3c; }"
    );
    
    importButton_ = new QPushButton("↓ Import");
    importButton_->setToolTip("Import Script (Ctrl+O)");
    importButton_->setStyleSheet(
        "QPushButton { padding: 6px; }"
        "QPushButton:hover { background-color: #3c3c3c; }"
    );
    
    bottomButtonLayout->addWidget(newButton_);
    bottomButtonLayout->addWidget(importButton_);

    leftLayout->addWidget(scriptsLabel);
    leftLayout->addWidget(scriptListWidget_, 1);  // stretch factor 1
    leftLayout->addLayout(bottomButtonLayout);

    // =========================================================================
    // Right panel: editor + output (vertical splitter)
    // =========================================================================
    rightSplitter_ = new QSplitter(Qt::Vertical);

    // Editor panel
    auto* editorPanel = new QWidget();
    auto* editorLayout = new QVBoxLayout(editorPanel);
    editorLayout->setContentsMargins(0, 0, 0, 0);
    editorLayout->setSpacing(4);

    // Editor header with run button
    auto* editorHeader = new QHBoxLayout();
    editorHeader->setSpacing(8);
    
    auto* editorLabel = new QLabel("Editor:");
    editorLabel->setStyleSheet("font-weight: bold;");
    
    runButton_ = new QPushButton("▶ Run");
    runButton_->setToolTip("Run Script (Ctrl+Enter)");
    runButton_->setStyleSheet(
        "QPushButton { background-color: #4CAF50; color: white; padding: 4px 12px; font-weight: bold; }"
        "QPushButton:hover { background-color: #45a049; }"
        "QPushButton:disabled { background-color: #888; }"
    );
    
    // Status dot (green=ready, blue=running, red=error)
    statusLabel_ = new QLabel("●");
    statusLabel_->setToolTip("Ready");
    statusLabel_->setStyleSheet("color: #4CAF50; font-size: 14px;");  // Green dot
    
    editorHeader->addWidget(editorLabel);
    editorHeader->addStretch();
    editorHeader->addWidget(statusLabel_);
    editorHeader->addWidget(runButton_);
    
    editorWidget_ = new CodeEditor();
    editorWidget_->setPlaceholderText("// Write your JavaScript here. Access OBS API via 'obs' global");
    
    editorWidget_->setTabStopDistance(40);
    editorWidget_->setLineWrapMode(QPlainTextEdit::NoWrap);
    
    // Dark background styling with JetBrains Mono font
    editorWidget_->setStyleSheet(
        "QPlainTextEdit {"
        "  font-family: 'JetBrains Mono', 'Cascadia Code', 'Fira Code', Consolas, monospace;"
        "  font-size: 10pt;"
        "  background-color: #1e1e1e;"
        "  color: #d4d4d4;"
        "  border: 1px solid #3c3c3c;"
        "  border-radius: 4px;"
        "  padding: 8px;"
        "  selection-background-color: #264f78;"
        "}"
    );
    
    // Set placeholder text color (visible on dark background)
    QPalette editorPalette = editorWidget_->palette();
    editorPalette.setColor(QPalette::PlaceholderText, QColor(128, 128, 128));
    editorWidget_->setPalette(editorPalette);

    editorLayout->addLayout(editorHeader);
    editorLayout->addWidget(editorWidget_, 1);

    // Output panel
    auto* outputPanel = new QWidget();
    auto* outputLayout = new QVBoxLayout(outputPanel);
    outputLayout->setContentsMargins(0, 0, 0, 0);
    outputLayout->setSpacing(4);

    auto* outputLabel = new QLabel("Output:");
    outputLabel->setStyleSheet("font-weight: bold;");
    
    outputWidget_ = new QPlainTextEdit();
    outputWidget_->setReadOnly(true);
    outputWidget_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    
    // Enable context menu for output
    outputWidget_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(outputWidget_, &QWidget::customContextMenuRequested, [this](const QPoint& pos) {
        QMenu contextMenu(outputWidget_);
        QAction* clearAction = contextMenu.addAction("Clear Output");
        connect(clearAction, &QAction::triggered, this, &ScriptEditorDialog::clearOutput);
        contextMenu.exec(outputWidget_->mapToGlobal(pos));
    });
    
    // Dark background styling with JetBrains Mono font
    outputWidget_->setStyleSheet(
        "QPlainTextEdit {"
        "  font-family: 'JetBrains Mono', 'Cascadia Code', 'Fira Code', Consolas, monospace;"
        "  font-size: 10pt;"
        "  background-color: #1a1a1a;"
        "  color: #cccccc;"
        "  border: 1px solid #3c3c3c;"
        "  border-radius: 4px;"
        "  padding: 8px;"
        "}"
    );
    
    // Install event filter for Ctrl+Scroll zoom (on viewport to catch wheel events)
    outputWidget_->viewport()->installEventFilter(this);

    outputLayout->addWidget(outputLabel);
    outputLayout->addWidget(outputWidget_, 1);

    rightSplitter_->addWidget(editorPanel);
    rightSplitter_->addWidget(outputPanel);
    rightSplitter_->setStretchFactor(0, 3);
    rightSplitter_->setStretchFactor(1, 1);

    mainSplitter_->addWidget(leftPanel);
    mainSplitter_->addWidget(rightSplitter_);
    mainSplitter_->setStretchFactor(0, 0);
    mainSplitter_->setStretchFactor(1, 1);
    mainSplitter_->setSizes({200, 700});

    // Connect signals
    connect(runButton_, &QPushButton::clicked, this, &ScriptEditorDialog::runScript);
    connect(newButton_, &QPushButton::clicked, this, &ScriptEditorDialog::newScript);
    connect(importButton_, &QPushButton::clicked, this, &ScriptEditorDialog::importScript);
    connect(scriptListWidget_, &QListWidget::currentItemChanged, this, &ScriptEditorDialog::onScriptSelected);

    mainLayout->addWidget(mainSplitter_, 1);
}

void ScriptEditorDialog::setupShortcuts()
{
    // Ctrl+Enter to run
    auto* runShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this);
    connect(runShortcut, &QShortcut::activated, this, &ScriptEditorDialog::runScript);
    
    // Ctrl+Shift+Enter also
    auto* runShortcut2 = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Return), this);
    connect(runShortcut2, &QShortcut::activated, this, &ScriptEditorDialog::runScript);
    
    // Ctrl+N for new script
    auto* newShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_N), this);
    connect(newShortcut, &QShortcut::activated, this, &ScriptEditorDialog::newScript);
    
    // Ctrl+S to save
    auto* saveShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_S), this);
    connect(saveShortcut, &QShortcut::activated, [this]() {
        saveCurrentScript();
        saveScripts();
        statusLabel_->setToolTip("Saved");
        statusLabel_->setStyleSheet("color: #4CAF50; font-size: 14px;");
    });
    
    // Ctrl++ to zoom in editor
    auto* zoomInShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Plus), this);
    connect(zoomInShortcut, &QShortcut::activated, [this]() {
        editorWidget_->zoomIn();
    });
    
    auto* zoomInShortcut2 = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Equal), this);
    connect(zoomInShortcut2, &QShortcut::activated, [this]() {
        editorWidget_->zoomIn();
    });
    
    // Ctrl+- to zoom out editor
    auto* zoomOutShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Minus), this);
    connect(zoomOutShortcut, &QShortcut::activated, [this]() {
        editorWidget_->zoomOut();
    });
    
    // Ctrl+0 to reset zoom
    auto* resetZoomShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_0), this);
    connect(resetZoomShortcut, &QShortcut::activated, [this]() {
        editorWidget_->setFontSize(10);
    });
    
    // Ctrl+O to import script
    auto* importShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_O), this);
    connect(importShortcut, &QShortcut::activated, this, &ScriptEditorDialog::importScript);
    
    // F2 to rename script
    auto* renameShortcut = new QShortcut(QKeySequence(Qt::Key_F2), this);
    connect(renameShortcut, &QShortcut::activated, this, &ScriptEditorDialog::renameScript);
    
    // Ctrl+D to duplicate script
    auto* duplicateShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_D), this);
    connect(duplicateShortcut, &QShortcut::activated, this, &ScriptEditorDialog::duplicateScript);
    
    // Ctrl+E to export script
    auto* exportShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_E), this);
    connect(exportShortcut, &QShortcut::activated, this, &ScriptEditorDialog::exportScript);
    
    // Delete key to delete script (only when script list is focused)
    auto* deleteShortcut = new QShortcut(QKeySequence(Qt::Key_Delete), scriptListWidget_);
    connect(deleteShortcut, &QShortcut::activated, this, &ScriptEditorDialog::deleteScript);
}

void ScriptEditorDialog::newScript() {
    bool ok;
    QString name = QInputDialog::getText(this, "New Script", "Script name:", 
                                         QLineEdit::Normal, "NewScript", &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    
    name = name.trimmed();
    
    // Deduplicate name
    QString baseName = name;
    int counter = 1;
    while (scripts_.contains(name)) {
        name = baseName + QString::number(counter++);
    }
    
    saveCurrentScript();
    
    scripts_[name] = "// " + name + "\n\n";
    scriptListWidget_->addItem(name);
    
    // Select the new script
    auto items = scriptListWidget_->findItems(name, Qt::MatchExactly);
    if (!items.isEmpty()) {
        scriptListWidget_->setCurrentItem(items.first());
    }
    
    saveScripts();
}

void ScriptEditorDialog::renameScript() {
    if (currentScriptName_.isEmpty()) return;
    
    bool ok;
    QString newName = QInputDialog::getText(this, "Rename Script", "New name:", 
                                            QLineEdit::Normal, currentScriptName_, &ok);
    if (!ok || newName.trimmed().isEmpty()) return;
    
    newName = newName.trimmed();
    if (newName == currentScriptName_) return;
    
    // Check for duplicate
    if (scripts_.contains(newName)) {
        QMessageBox::warning(this, "Rename Failed", "A script with that name already exists.");
        return;
    }
    
    // Rename
    QString content = scripts_.take(currentScriptName_);
    scripts_[newName] = content;
    
    // Update list
    auto items = scriptListWidget_->findItems(currentScriptName_, Qt::MatchExactly);
    if (!items.isEmpty()) {
        items.first()->setText(newName);
    }
    
    currentScriptName_ = newName;
    saveScripts();
}

void ScriptEditorDialog::deleteScript() {
    if (currentScriptName_.isEmpty()) return;
    
    if (scripts_.size() <= 1) {
        QMessageBox::warning(this, "Cannot Delete", "Cannot delete the last script.");
        return;
    }
    
    int result = QMessageBox::question(this, "Delete Script", 
                                       QString("Delete '%1'?").arg(currentScriptName_),
                                       QMessageBox::Yes | QMessageBox::No);
    if (result != QMessageBox::Yes) return;
    
    scripts_.remove(currentScriptName_);
    
    // Remove from list
    auto items = scriptListWidget_->findItems(currentScriptName_, Qt::MatchExactly);
    if (!items.isEmpty()) {
        delete scriptListWidget_->takeItem(scriptListWidget_->row(items.first()));
    }
    
    currentScriptName_.clear();
    
    // Select first remaining
    if (scriptListWidget_->count() > 0) {
        scriptListWidget_->setCurrentRow(0);
    }
    
    saveScripts();
}

void ScriptEditorDialog::duplicateScript() {
    if (currentScriptName_.isEmpty()) return;
    
    // Generate unique name
    QString baseName = currentScriptName_ + "_copy";
    QString newName = baseName;
    int counter = 1;
    while (scripts_.contains(newName)) {
        newName = baseName + QString::number(counter++);
    }
    
    // Copy content
    scripts_[newName] = scripts_[currentScriptName_];
    scriptListWidget_->addItem(newName);
    
    // Select the duplicate
    auto items = scriptListWidget_->findItems(newName, Qt::MatchExactly);
    if (!items.isEmpty()) {
        scriptListWidget_->setCurrentItem(items.first());
    }
    
    saveScripts();
}

void ScriptEditorDialog::exportScript() {
    if (currentScriptName_.isEmpty()) return;
    
    QString fileName = QFileDialog::getSaveFileName(
        this, 
        "Export Script", 
        currentScriptName_ + ".js",
        "JavaScript Files (*.js);;All Files (*)"
    );
    
    if (fileName.isEmpty()) return;
    
    QFile file(fileName);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(scripts_[currentScriptName_].toUtf8());
        file.close();
        appendOutput(QString("Exported to: %1").arg(fileName));
    } else {
        appendOutput(QString("Failed to export: %1").arg(file.errorString()), true);
    }
}

void ScriptEditorDialog::importScript() {
    QString fileName = QFileDialog::getOpenFileName(
        this, 
        "Import Script", 
        QString(),
        "JavaScript Files (*.js);;All Files (*)"
    );
    
    if (fileName.isEmpty()) return;
    
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        appendOutput(QString("Failed to open: %1").arg(file.errorString()), true);
        return;
    }
    
    QString content = QString::fromUtf8(file.readAll());
    file.close();
    
    // Use filename (without extension) as script name
    QFileInfo fileInfo(fileName);
    QString baseName = fileInfo.baseName();
    QString name = baseName;
    
    // Deduplicate name
    int counter = 1;
    while (scripts_.contains(name)) {
        name = baseName + QString::number(counter++);
    }
    
    saveCurrentScript();
    
    scripts_[name] = content;
    scriptListWidget_->addItem(name);
    
    // Select the imported script
    auto items = scriptListWidget_->findItems(name, Qt::MatchExactly);
    if (!items.isEmpty()) {
        scriptListWidget_->setCurrentItem(items.first());
    }
    
    saveScripts();
    appendOutput(QString("Imported: %1").arg(fileName));
}

void ScriptEditorDialog::onScriptSelected(QListWidgetItem* current, QListWidgetItem* previous) {
    Q_UNUSED(previous);
    if (current) {
        switchToScript(current->text());
    }
}

void ScriptEditorDialog::runScript()
{
    // Check if Node.js is initialized
    if (!obs_node::is_initialized()) {
        appendOutput("Error: Node.js runtime not initialized.", true);
        statusLabel_->setToolTip("Not initialized");
        statusLabel_->setStyleSheet("color: #F44336; font-size: 14px;");
        return;
    }

    // Check if already running
    if (currentRunner_ && currentRunner_->is_running()) {
        appendOutput("Script already running. Please wait or terminate.", true);
        return;
    }

    // Save before running
    saveCurrentScript();

    QString script = editorWidget_->toPlainText();
    if (script.trimmed().isEmpty()) {
        appendOutput("No script to run.", true);
        return;
    }

    statusLabel_->setToolTip("Running...");
    statusLabel_->setStyleSheet("color: #2196F3; font-size: 14px;");
    runButton_->setEnabled(false);

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
                    statusLabel_->setToolTip("Completed");
                    statusLabel_->setStyleSheet("color: #4CAF50; font-size: 14px;");
                } else {
                    QString errorMsg = QString::fromStdString(error);
                    if (errorMsg.isEmpty()) {
                        errorMsg = "Unknown error";
                    }
                    appendOutput(errorMsg, true);
                    statusLabel_->setToolTip("Error");
                    statusLabel_->setStyleSheet("color: #F44336; font-size: 14px;");
                }
            }, Qt::QueuedConnection);
        };

        // Run the script
        if (!currentRunner_->run(script.toStdString(), options)) {
            appendOutput("Failed to start script execution.", true);
            statusLabel_->setToolTip("Failed");
            statusLabel_->setStyleSheet("color: #F44336; font-size: 14px;");
            runButton_->setEnabled(true);
            return;
        }

        // Don't wait synchronously - let it run async

    } catch (const std::exception& e) {
        appendOutput(QString("Exception: %1").arg(e.what()), true);
        statusLabel_->setToolTip("Exception");
        statusLabel_->setStyleSheet("color: #F44336; font-size: 14px;");
        runButton_->setEnabled(true);
    } catch (...) {
        appendOutput("Unknown exception occurred.", true);
        statusLabel_->setToolTip("Exception");
        statusLabel_->setStyleSheet("color: #F44336; font-size: 14px;");
        runButton_->setEnabled(true);
    }
}

void ScriptEditorDialog::clearOutput()
{
    outputWidget_->clear();
    statusLabel_->setToolTip("Ready");
    statusLabel_->setStyleSheet("color: #4CAF50; font-size: 14px;");
}

void ScriptEditorDialog::appendOutput(const QString& text, bool isError)
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
