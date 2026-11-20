#include "AccountDialog.h"

#include "Passwords.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

QComboBox *securityBox(QWidget *parent, ImapClient::Security current)
{
    auto *box = new QComboBox(parent);
    for (ImapClient::Security s : {ImapClient::Security::Tls, ImapClient::Security::StartTls, ImapClient::Security::None})
        box->addItem(securityName(s), int(s));
    box->setCurrentIndex(box->findData(int(current)));
    return box;
}

QSpinBox *portBox(QWidget *parent, int port)
{
    auto *box = new QSpinBox(parent);
    box->setRange(1, 65535);
    box->setValue(port);
    return box;
}

} // namespace

AccountDialog::AccountDialog(const Account &account, QWidget *parent)
    : QDialog(parent)
    , m_account(account)
    , m_isNew(account.id.isEmpty())
{
    setWindowTitle(m_isNew ? tr("Add an Account") : tr("Account Settings"));
    auto *layout = new QVBoxLayout(this);

    auto *you = new QFormLayout;
    m_email = new QLineEdit(account.email, this);
    m_email->setPlaceholderText(QStringLiteral("maria@example.com"));
    m_name = new QLineEdit(account.name, this);
    m_name->setPlaceholderText(tr("What others see as the sender"));
    m_password = new QLineEdit(this);
    m_password->setEchoMode(QLineEdit::Password);
    m_password->setPlaceholderText(m_isNew ? tr("Your mail password, or an app password") : tr("Unchanged"));
    you->addRow(tr("Email address"), m_email);
    you->addRow(tr("Your name"), m_name);
    you->addRow(tr("Password"), m_password);
    layout->addLayout(you);

    auto *hint = new QLabel(tr("Gmail, Outlook, Yahoo and iCloud need an <i>app password</i> made in your account's security settings "
                               "when two-step sign-in is on. The password is kept in your desktop's keyring."),
                            this);
    hint->setWordWrap(true);
    hint->setTextFormat(Qt::RichText);
    layout->addWidget(hint);

    auto *incoming = new QGroupBox(tr("Incoming mail (IMAP)"), this);
    auto *in = new QFormLayout(incoming);
    m_imapHost = new QLineEdit(account.imapHost, incoming);
    m_imapPort = portBox(incoming, account.imapPort);
    m_imapSecurity = securityBox(incoming, account.imapSecurity);
    m_imapUser = new QLineEdit(account.imapUser, incoming);
    m_imapUser->setPlaceholderText(tr("The email address"));
    in->addRow(tr("Server"), m_imapHost);
    in->addRow(tr("Port"), m_imapPort);
    in->addRow(tr("Encryption"), m_imapSecurity);
    in->addRow(tr("User name"), m_imapUser);
    layout->addWidget(incoming);

    auto *outgoing = new QGroupBox(tr("Outgoing mail (SMTP)"), this);
    auto *out = new QFormLayout(outgoing);
    m_smtpHost = new QLineEdit(account.smtpHost, outgoing);
    m_smtpPort = portBox(outgoing, account.smtpPort);
    m_smtpSecurity = securityBox(outgoing, account.smtpSecurity);
    m_smtpUser = new QLineEdit(account.smtpUser, outgoing);
    m_smtpUser->setPlaceholderText(tr("The same as for incoming mail"));
    m_smtpPassword = new QLineEdit(outgoing);
    m_smtpPassword->setEchoMode(QLineEdit::Password);
    m_smtpPassword->setPlaceholderText(tr("The same as above"));
    out->addRow(tr("Server"), m_smtpHost);
    out->addRow(tr("Port"), m_smtpPort);
    out->addRow(tr("Encryption"), m_smtpSecurity);
    out->addRow(tr("User name"), m_smtpUser);
    out->addRow(tr("Password"), m_smtpPassword);
    layout->addWidget(outgoing);

    // Ports follow the encryption chosen, as long as they are the usual ones.
    connect(m_imapSecurity, &QComboBox::currentIndexChanged, this, [this] {
        const auto s = ImapClient::Security(m_imapSecurity->currentData().toInt());
        if (m_imapPort->value() == 993 || m_imapPort->value() == 143)
            m_imapPort->setValue(s == ImapClient::Security::Tls ? 993 : 143);
    });
    connect(m_smtpSecurity, &QComboBox::currentIndexChanged, this, [this] {
        const auto s = ImapClient::Security(m_smtpSecurity->currentData().toInt());
        if (m_smtpPort->value() == 465 || m_smtpPort->value() == 587 || m_smtpPort->value() == 25)
            m_smtpPort->setValue(s == ImapClient::Security::Tls ? 465 : 587);
    });
    if (m_isNew)
        connect(m_email, &QLineEdit::editingFinished, this, &AccountDialog::guessServers);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(m_isNew ? tr("Add Account") : tr("Save"));
    connect(buttons, &QDialogButtonBox::accepted, this, &AccountDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    resize(460, sizeHint().height());
}

void AccountDialog::guessServers()
{
    const QString email = m_email->text().trimmed();
    if (!email.contains(QLatin1Char('@')) || !m_imapHost->text().isEmpty())
        return;
    const Account guess = Account::guessFor(email);
    m_imapHost->setText(guess.imapHost);
    m_imapPort->setValue(guess.imapPort);
    m_imapSecurity->setCurrentIndex(m_imapSecurity->findData(int(guess.imapSecurity)));
    m_smtpHost->setText(guess.smtpHost);
    m_smtpSecurity->setCurrentIndex(m_smtpSecurity->findData(int(guess.smtpSecurity)));
    m_smtpPort->setValue(guess.smtpPort);
}

void AccountDialog::accept()
{
    const QString email = m_email->text().trimmed();
    if (!email.contains(QLatin1Char('@')) || email.contains(QLatin1Char(' '))) {
        QMessageBox::warning(this, windowTitle(), tr("Enter your email address."));
        return;
    }
    if (m_imapHost->text().trimmed().isEmpty() || m_smtpHost->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, windowTitle(), tr("Enter the incoming and outgoing servers."));
        return;
    }
    if (m_isNew && m_password->text().isEmpty()) {
        QMessageBox::warning(this, windowTitle(), tr("Enter the password."));
        return;
    }
    m_account.email = email;
    m_account.name = m_name->text().trimmed();
    m_account.imapHost = m_imapHost->text().trimmed();
    m_account.imapPort = quint16(m_imapPort->value());
    m_account.imapSecurity = ImapClient::Security(m_imapSecurity->currentData().toInt());
    m_account.imapUser = m_imapUser->text().trimmed();
    m_account.smtpHost = m_smtpHost->text().trimmed();
    m_account.smtpPort = quint16(m_smtpPort->value());
    m_account.smtpSecurity = ImapClient::Security(m_smtpSecurity->currentData().toInt());
    m_account.smtpUser = m_smtpUser->text().trimmed();
    Account::save(m_account);
    QString error;
    if (!m_password->text().isEmpty()
        && !Passwords::save(m_account.id, QStringLiteral("imap"), m_password->text(), email, nullptr, &error)) {
        QMessageBox::warning(this, windowTitle(), tr("The password couldn't be kept: %1").arg(error));
        return;
    }
    if (!m_smtpPassword->text().isEmpty()
        && !Passwords::save(m_account.id, QStringLiteral("smtp"), m_smtpPassword->text(), email, nullptr, &error)) {
        QMessageBox::warning(this, windowTitle(), tr("The password couldn't be kept: %1").arg(error));
        return;
    }
    QDialog::accept();
}
