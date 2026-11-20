#pragma once

#include <QDialog>

// Edit → Claude: the API key (kept with the passwords, in the keyring), the
// model, what has been spent, and where Claude's answers are kept (Argonaut's
// Preferences → Claude, for mail). Nothing is sent from here but the free key test.
class ClaudeDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ClaudeDialog(QWidget *parent = nullptr);

signals:
    void openReportsRequested();
};
