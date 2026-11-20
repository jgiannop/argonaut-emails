#pragma once

#include "Account.h"

#include <QDialog>

class QComboBox;
class QLineEdit;
class QSpinBox;

// Adds an account or changes one: the address, your name, the password
// (kept in the keyring), and the servers, guessed from the address for
// the big providers.
class AccountDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AccountDialog(const Account &account, QWidget *parent = nullptr);

    // The account as saved (OK pressed).
    Account account() const { return m_account; }

    void accept() override;

private:
    void guessServers();

    Account m_account;
    bool m_isNew;
    QLineEdit *m_email;
    QLineEdit *m_name;
    QLineEdit *m_password;
    QLineEdit *m_imapHost;
    QSpinBox *m_imapPort;
    QComboBox *m_imapSecurity;
    QLineEdit *m_imapUser;
    QLineEdit *m_smtpHost;
    QSpinBox *m_smtpPort;
    QComboBox *m_smtpSecurity;
    QLineEdit *m_smtpUser;
    QLineEdit *m_smtpPassword;
};
