#pragma once

#include <QDialog>

class QVBoxLayout;

// The Extensions window (Ctrl+Shift+X): every extension found, what it is,
// where it came from, whether it is running, and its switch. The list is
// Extensions::Host's; this window only shows it and flips switches.
class ExtensionsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ExtensionsDialog(QWidget *parent = nullptr);

signals:
    void openFolderRequested(const QString &folder); // the user's extensions folder
    void guideRequested();                           // the help topic

private:
    void rebuild();
    void updateStates();

    QVBoxLayout *m_list = nullptr;
};
