/**
 * script-editor-dialog.h - Node Script Editor for OBS
 * 
 * Multi-script editor with persistence for running JavaScript code
 * in the OBS Node.js environment.
 */

#pragma once

#include <QDialog>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QToolBar>
#include <QShortcut>
#include <QMenu>
#include <QFont>
#include <QTimer>
#include <QMap>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QInputDialog>
#include <QMessageBox>
#include <QFile>
#include <QPainter>
#include <QTextBlock>
#include <QWheelEvent>

#include <memory>

// Forward declaration
namespace obs_node { class ScriptRunner; }

// ============================================================================
// CodeEditor - QPlainTextEdit with line numbers
// ============================================================================
class LineNumberArea;

class CodeEditor : public QPlainTextEdit {
    Q_OBJECT

public:
    explicit CodeEditor(QWidget* parent = nullptr);
    
    void lineNumberAreaPaintEvent(QPaintEvent* event);
    int lineNumberAreaWidth();
    
    // Zoom functionality
    void zoomIn(int range = 1);
    void zoomOut(int range = 1);
    void setFontSize(int size);
    int fontSize() const { return fontSize_; }

protected:
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private slots:
    void updateLineNumberAreaWidth(int newBlockCount);
    void highlightCurrentLine();
    void updateLineNumberArea(const QRect& rect, int dy);

private:
    QWidget* lineNumberArea_;
    int fontSize_ = 10;
};

class LineNumberArea : public QWidget {
public:
    explicit LineNumberArea(CodeEditor* editor) : QWidget(editor), codeEditor_(editor) {}

    QSize sizeHint() const override {
        return QSize(codeEditor_->lineNumberAreaWidth(), 0);
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        codeEditor_->lineNumberAreaPaintEvent(event);
    }

private:
    CodeEditor* codeEditor_;
};

// ============================================================================
// ScriptEditorDialog - Main dialog
// ============================================================================
class ScriptEditorDialog : public QDialog {
    Q_OBJECT

public:
    explicit ScriptEditorDialog(QWidget* parent = nullptr);
    ~ScriptEditorDialog() override;

public slots:
    void runScript();
    void clearOutput();
    
    // Script management
    void newScript();
    void renameScript();
    void deleteScript();
    void duplicateScript();
    void exportScript();
    void importScript();
    void onScriptSelected(QListWidgetItem* current, QListWidgetItem* previous);

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void setupUI();
    void setupShortcuts();
    void appendOutput(const QString& text, bool isError = false);
    
    // Persistence
    QString getScriptsFilePath();
    void loadScripts();
    void saveScripts();
    void saveCurrentScript();
    void switchToScript(const QString& name);

    // UI Components
    QListWidget* scriptListWidget_ = nullptr;
    CodeEditor* editorWidget_ = nullptr;  // Changed from QPlainTextEdit
    QPlainTextEdit* outputWidget_ = nullptr;
    QPushButton* runButton_ = nullptr;
    QPushButton* newButton_ = nullptr;
    QPushButton* importButton_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QSplitter* mainSplitter_ = nullptr;
    QSplitter* rightSplitter_ = nullptr;
    
    // Script data
    QMap<QString, QString> scripts_;      // name -> content
    QString currentScriptName_;
    int outputFontSize_ = 10;  // Output widget font size
    
    // Script runner (kept alive for async execution)
    std::unique_ptr<obs_node::ScriptRunner> currentRunner_;
};
