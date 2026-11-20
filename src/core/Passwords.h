#pragma once

#include <QString>

// Where the accounts' passwords are kept (core code, no widgets): the
// desktop's keyring through the freedesktop Secret Service over D-Bus
// (GNOME Keyring, KWallet), one item per account and kind ("imap",
// "smtp"); without a keyring a file only you can read (0600). Never in the
// settings file. MAIL_KEYSTORE=file forces the file (tests: never the real
// keyring). The keyring code is Argonaut's KeyStore, made per account.
namespace Passwords {

enum class Where { None, Keyring, File };

QString load(const QString &account, const QString &kind, Where *where = nullptr);
// `label` names the account in the keyring's own window ("maria@example.com").
bool save(const QString &account, const QString &kind, const QString &password, const QString &label,
          Where *where = nullptr, QString *error = nullptr);
bool remove(const QString &account, const QString &kind);

QString describe(Where where);

} // namespace Passwords
