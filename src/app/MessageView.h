#pragma once

#include "Mime.h"
#include "Theme.h"

#include <QTextBrowser>
#include <QWidget>

class QLabel;
class QHBoxLayout;

// What a message's HTML may load: its own pictures (cid:) and nothing else.
class MailBrowser : public QTextBrowser
{
    Q_OBJECT

public:
    explicit MailBrowser(QWidget *parent = nullptr);
    void setMessage(const Mime::Message *message) { m_message = message; }

protected:
    QVariant loadResource(int type, const QUrl &name) override;

private:
    const Mime::Message *m_message = nullptr;
};

// One message: who, when, the subject, its text (the HTML made safe, else
// the plain text), and its attachments, each saved with a click.
class MessageView : public QWidget
{
    Q_OBJECT

public:
    explicit MessageView(QWidget *parent = nullptr);

    void showMessage(const QByteArray &raw);
    void showNote(const QString &text); // "Loading…", an error, or nothing chosen
    void applyTheme(const Theme &theme);
    const Mime::Message &message() const { return m_message; }
    bool hasMessage() const { return m_hasMessage; }
    QString shownText() const; // for tests

private:
    void saveAttachment(int index);

    Mime::Message m_message;
    bool m_hasMessage = false;
    QWidget *m_header = nullptr;
    QLabel *m_subject = nullptr;
    QLabel *m_from = nullptr;
    QLabel *m_to = nullptr;
    QLabel *m_date = nullptr;
    QLabel *m_note = nullptr;
    QWidget *m_files = nullptr;
    QHBoxLayout *m_filesLayout = nullptr;
    MailBrowser *m_body = nullptr;
    Theme m_theme;
};
