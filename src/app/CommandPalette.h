#pragma once

#include <QFrame>
#include <QList>
#include <QPointer>

class QAction;
class QLineEdit;
class QListWidget;

// Ctrl+Shift+P: type a few letters of any command and press Enter, like
// the command palette of code editors (Argonaut's, as it is there). A frame over the top of the window
// (not a separate window), with every enabled menu command, labelled with
// the menu it's in, and the ones you used last first.
class CommandPalette : public QFrame
{
    Q_OBJECT

public:
    struct Command
    {
        QString name;  // "By Name"
        QString where; // "View › Sort"
        QAction *action = nullptr;
    };

    explicit CommandPalette(QWidget *parent);

    void open(const QList<Command> &commands);
    const QList<Command> &commands() const { return m_commands; } // what it was opened with (tests)
    void close();

    // How well `query` matches `text` (both folded): 0 = not at all. Words
    // match anywhere; whole-word and early matches score higher, and as a
    // last resort the letters may be spread out ("wn" finds "Write New").
    static int score(const QString &text, const QString &query);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void filter();
    void runCurrent();
    void place();

    QList<Command> m_commands;
    QStringList m_recent; // names of the last commands run, newest first
    QLineEdit *m_edit = nullptr;
    QListWidget *m_list = nullptr;
    QPointer<QWidget> m_returnFocus;
};
