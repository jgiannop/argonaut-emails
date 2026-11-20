#pragma once

#include "Account.h"
#include "Composer.h"

#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class SmtpClient;

// A message being written: From (the accounts), To, Cc, Bcc, Subject, the
// text and the files attached. Send hands it to the account's SMTP server;
// the window stays, with what was written, if that fails.
class ComposeWindow : public QWidget
{
    Q_OBJECT

public:
    explicit ComposeWindow(const QString &accountId, QWidget *parent = nullptr);

    void setTo(const QString &to);
    void setCc(const QString &cc);
    void setSubject(const QString &subject);
    void setText(const QString &text);
    void setReplyTo(const QByteArray &messageId, const QByteArray &references);
    void addFile(const Composer::File &file);
    // Tests: the SMTP settings instead of the account's.
    void setSmtpSettings(const SmtpClient::Settings &settings);

    // What would be sent now.
    Composer::Draft draft() const;
    Account account() const;

signals:
    // Sent: the account it went from and the message as sent.
    void sent(const QString &accountId, const QByteArray &raw);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void send();
    void attach();
    void setSending(bool sending);

    QComboBox *m_from;
    QLineEdit *m_to;
    QLineEdit *m_cc;
    QLineEdit *m_bcc;
    QLineEdit *m_subject;
    QPlainTextEdit *m_text;
    QListWidget *m_files;
    QPushButton *m_send;
    QLabel *m_status;
    QList<Composer::File> m_attached;
    QByteArray m_inReplyTo;
    QByteArray m_references;
    SmtpClient *m_smtp = nullptr;
    SmtpClient::Settings m_testSettings;
    bool m_haveTestSettings = false;
    bool m_sending = false;
    bool m_done = false;
};
