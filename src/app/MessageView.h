#pragma once

#include "Mime.h"
#include "Theme.h"

#include <QWidget>

class MailWebView;
class QHBoxLayout;
class QLabel;
class QPushButton;

// One message: who, when, the subject, its text (the HTML made safe, else
// the plain text), and its attachments, each saved with a click.
class MessageView : public QWidget
{
    Q_OBJECT

public:
    explicit MessageView(QWidget *parent = nullptr);

    void showMessage(const QByteArray &raw);
    void showNote(const QString &text); // "Loading…", an error, or nothing chosen
    // What an extension wrote (Claude's report), as HTML made safe: a title
    // over it, a line under the title, in the theme's colours.
    void showResult(const QString &title, const QString &html, const QString &note);
    bool hasResult() const { return m_hasResult; }
    void applyTheme(const Theme &theme);
    const Mime::Message &message() const { return m_message; }
    bool hasMessage() const { return m_hasMessage; }
    QString shownText() const; // for tests: the text shown, once it is there
    bool picturesShown() const; // from the internet, after "Show pictures"
    MailWebView *body() const { return m_body; }

private:
    void saveAttachment(int index);
    void showBody(bool remotePictures);

    Mime::Message m_message;
    bool m_hasMessage = false;
    QString m_noteText; // the note shown, to show again in new colours
    bool m_hasResult = false;
    QString m_resultHtml;
    QWidget *m_header = nullptr;
    QLabel *m_subject = nullptr;
    QLabel *m_from = nullptr;
    QLabel *m_to = nullptr;
    QLabel *m_date = nullptr;
    QWidget *m_noteBar = nullptr; // "3 pictures from the internet weren't loaded"  [Show pictures]
    QLabel *m_note = nullptr;
    QPushButton *m_showPictures = nullptr;
    QWidget *m_files = nullptr;
    QHBoxLayout *m_filesLayout = nullptr;
    MailWebView *m_body = nullptr;
    Theme m_theme;
};
