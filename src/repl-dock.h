/**
 * repl-dock.h - REPL dock widget for OBS
 * 
 * Provides an interactive REPL (Read-Eval-Print Loop) interface
 * in the OBS docks menu for running Node.js commands.
 */

#pragma once

#include <QWidget>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QKeyEvent>
#include <QStringList>

class ReplDock : public QWidget {
    Q_OBJECT

public:
    explicit ReplDock(QWidget* parent = nullptr);
    ~ReplDock() override;

public slots:
    void executeCommand();
    void clearOutput();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void setupUI();
    void appendOutput(const QString& text, bool isError = false);
    void appendResult(const QString& text);
    void historyUp();
    void historyDown();

    QPlainTextEdit* outputWidget_ = nullptr;
    QLineEdit* inputWidget_ = nullptr;
    
    QStringList commandHistory_;
    int historyIndex_ = -1;
};
