#pragma once

#include "MailStore.h"
#include "Theme.h"

#include <QTreeWidget>

// The accounts and their folders, at the left: an account's row, then its
// folders as the server nests them, with the number of unread messages.
class FolderTree : public QTreeWidget
{
    Q_OBJECT

public:
    explicit FolderTree(MailStore *store, QWidget *parent = nullptr);

    // Builds again from the store (the selection is kept).
    void reload();
    void applyTheme(const Theme &theme);
    void select(const QString &account, const QByteArray &path);

    QString currentAccount() const;
    QByteArray currentFolder() const; // empty on an account's row

signals:
    void folderChosen(const QString &account, const QByteArray &path);

private:
    void updateCounts(const QString &account);

    MailStore *m_store;
    Theme m_theme;
    bool m_reloading = false;
};
