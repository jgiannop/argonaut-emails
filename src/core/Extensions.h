#pragma once

#include "Claude.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

class MailReader;
class MailStore;
class QProcess;
class Sync;

// Extensions, as in Argonaut (src/Extensions.h there; docs/extensions.md
// here is the guide for people writing one): separate programs that add
// commands to the program (core code, no widgets).
//
// An extension is a folder with an "extension.json" and a program. The
// program is started and talked to in JSON, one message per line, on its
// standard input and output. It offers commands (in a menu of the menu bar,
// and so in the command palette, or on the selected messages); it is told
// which one the user chose, and it answers with a line to show, a result for
// the message pane (Markdown), a form for the user to fill in. It can have
// Claude asked something through the program, with the user's key, and
// read the user's mail through the program: both only when its
// extension.json says so ("uses": ["claude", "mail"]), and only while it
// does what the user chose.
//
// An extension is a program with the user's rights: one from the user's own
// folder runs only after it is switched on (Edit → Extensions). What it
// sends is data: it is checked and cut to size. A crash, hang or nonsense
// from an extension never stops the program.
namespace Extensions {

constexpr int ProtocolVersion = 1;

// Its name in the command palette: "ext::Summarize Last Week with Claude",
// so what comes from an extension can be told apart (typing "ext" lists them).
QString paletteName(const QString &label);

struct Command
{
    QString extension; // its extension's id
    QString id;
    QString label;
    QString confirm;   // asked before it runs; empty: not asked
    QString group;     // the submenu it is in ("Claude", "Claude/Reports"); empty: the menu itself
    bool checked = false;
    bool asks = false;         // wants a text from the user first
    QString askLabel;
    QString askText;
    bool askMultiline = false;
    // "file", "edit", "view" or "message": in that menu of the menu bar (and
    // so in the command palette); about no message. Empty: on the selected
    // messages (their right-click menu).
    QString menu;
    bool multiple = true; // on messages: false = exactly one
};

// Something an extension needs the user to fill in or decide while it works:
// shown as a dialog that says which extension is asking.
struct FormField
{
    enum Kind { Text, Secret, Check, Choice } kind = Text;
    QString id;
    QString label;
    QString value;       // what it starts with ("true" for a ticked Check)
    QString placeholder; // Text: shown while empty
    QStringList choices; // Choice: what to pick from; Text: suggestions
    bool alone = false;  // Check: while ticked, the other fields are off
};

struct Form
{
    QString extension; // its extension's id
    QString extensionName;
    QString id;
    QString title;
    QString heading;
    QString text;
    QString warning;
    QList<FormField> fields;
    QStringList buttons; // besides Cancel; none given: "OK"
};

// A result for the message pane ("show"): text an extension wrote, never
// drawn by it. Markdown is turned into HTML here and shown locked, as a message is.
struct Result
{
    enum Kind { None, Text, Markdown } kind = None;
    QString extension; // its id
    QString title;
    QString text;
    QString note; // one line under it ("Cost: $0.03")
};

struct Manifest
{
    QString id;   // letters, digits, '-', '.', '_'
    QString name;
    QString description;
    QString version;
    QString author;
    QString folder;         // where extension.json is
    bool usesClaude = false; // "uses": ["claude"]: asks Claude through the program (the user's key and cost)
    bool readsMail = false;  // "uses": ["mail"]: reads the user's messages through the program
    bool whenNeeded = false; // "start": "when-needed": started when one of its commands is chosen
    QList<Command> commands; // "commands": shown while its program isn't running
    QString program;         // absolute path of the program
    QStringList arguments;
    bool builtIn = false;    // came with the program: on unless switched off
    QString problem;         // why it can't be used; empty when fine

    bool isUsable() const { return problem.isEmpty(); }
};

// Reads <folder>/extension.json. `problem` says what is wrong, if anything.
Manifest readManifest(const QString &folder, bool builtIn);

// Where the user puts extensions: $XDG_DATA_HOME/Argonaut/argonaut-emails/extensions.
QString userFolder();

class Host : public QObject
{
    Q_OBJECT

public:
    static Host *instance();
    explicit Host(QObject *parent = nullptr);
    ~Host() override;

    // Where an extension's "mail" questions are answered from: the store,
    // and each account's sync for the messages' text not kept yet.
    void setMail(MailStore *store, std::function<Sync *(const QString &account)> syncFor);

    QList<Manifest> extensions() const;
    void rescan();
    bool isEnabled(const QString &id) const;
    void setEnabled(const QString &id, bool on);

    // Starts what isn't started only when needed (the window is there).
    void start();
    // Stops every program, for good (the application is quitting).
    void shutdown();

    // Commands for the selected messages (`count` of them).
    QList<Command> commands(int count) const;
    // Commands for a menu of the menu bar: "file", "edit", "view", "message".
    QList<Command> menuCommands(const QString &menu) const;
    // Tells the extension to do it (after its question, if it has one).
    // `selection`: the chosen messages ({account, folder, uid, subject…}).
    void invoke(const Command &command, const QJsonArray &selection = {}, const QString &input = QString());
    QString nameOf(const QString &id) const;

    // Forms: formRequested says an extension asks; one window takes it
    // (takeForm) and the answer goes back once (answerForm).
    bool takeForm(const QString &extension, const QString &id);
    void answerForm(const QString &extension, const QString &id, bool cancelled, const QHash<QString, QString> &values = {},
                    int button = 0);

    // The user doesn't want to wait for Claude's answer any longer.
    void stopWaitingForClaude(const QString &extension);
    // What has been paid for an extension's questions to Claude, in dollars.
    static double claudeSpent(const QString &extension);

    enum class State { Unusable, Off, Waiting, Running, Stopped };
    State state(const QString &id) const;
    bool isRunning(const QString &id) const;

    static constexpr int MaxMailDays = 31;
    static constexpr int MaxMailMessages = 500;
    static constexpr int MaxMailChars = 4000; // of one message's text

signals:
    void extensionsChanged();
    void message(const QString &extension, const QString &text); // a line for the status bar
    void formRequested(const Extensions::Form &form);
    void showRequested(const Extensions::Result &result);
    // Claude is being asked something for an extension (`waiting` questions
    // without an answer yet; 0: none any more).
    void claudeWaiting(const QString &extension, const QString &name, int waiting);

private:
    struct Running
    {
        Manifest manifest;
        QProcess *process = nullptr;
        QByteArray buffer;
        int failures = 0;
        bool stopping = false;
        QList<Command> commands;
        bool commandsSaid = false;
        qint64 userActed = 0;  // when the user last chose something of its (ms since the epoch)
        qint64 lastActive = 0; // a when-needed one: when it last had something to do
        QStringList claudeWaiting;
        int claudePending = 0;
        int claudeAsked = 0;
        qint64 claudeFor = 0;
        int mailPending = 0;
        int mailAsked = 0;
        qint64 mailFor = 0;
        QString formPending;
        bool formTaken = false;
    };

    void launch(Running *ext);
    void stop(Running *ext);
    void send(Running *ext, const QJsonObject &message);
    void readFrom(Running *ext);
    void handle(Running *ext, const QByteArray &line);
    void hearForm(Running *ext, const QJsonObject &message);
    void hearShow(Running *ext, const QJsonObject &message);
    void askClaude(Running *ext, const QJsonObject &message);
    void readMail(Running *ext, const QJsonObject &message);
    Running *running(const QString &id) const; // with its program running
    static bool isActingForUser(const Running *ext);
    QList<Command> commandsOf(const Running *ext) const;
    bool isAvailable(const Running *ext) const;
    void wake(Running *ext);
    void stopIdle();
    void exited(Running *ext);
    void forget(Running *ext);

    QList<Manifest> m_manifests;
    QList<Running *> m_running; // the switched-on, usable ones, in order
    bool m_started = false;
    bool m_quitting = false;
    int m_idleMs = 60000; // MAIL_EXTENSION_IDLE_MS in tests
    Claude::Client *m_claude = nullptr;
    MailReader *m_mail = nullptr;
};

} // namespace Extensions

Q_DECLARE_METATYPE(Extensions::Form)
Q_DECLARE_METATYPE(Extensions::Result)
