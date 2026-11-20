#pragma once

#include "MailStore.h"

#include <QAbstractTableModel>
#include <QSortFilterProxyModel>
#include <QTreeView>

// The messages of one folder, newest first: ★, From (To in Sent and
// Drafts), Subject, Date. Unread ones are bold.
class MessageModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column { StarColumn, PeopleColumn, SubjectColumn, DateColumn, ColumnCount };
    enum { SummaryRole = Qt::UserRole + 1, SearchRole };

    explicit MessageModel(MailStore *store, QObject *parent = nullptr);

    void setFolder(const QString &account, const QByteArray &path, bool showRecipients);
    // Gmail's inbox tabs: only those of one ("primary"…); empty: all.
    void setCategory(const QString &category);
    QString category() const { return m_category; }
    void reload();
    QString account() const { return m_account; }
    QByteArray folder() const { return m_folder; }
    MailStore::Summary summaryAt(int row) const { return m_rows.value(row); }
    int rowOf(qint64 uid) const;

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    MailStore *m_store;
    QString m_account;
    QByteArray m_folder;
    bool m_recipients = false;
    QString m_category;
    QList<MailStore::Summary> m_rows;
};

class MessageList : public QTreeView
{
    Q_OBJECT

public:
    explicit MessageList(MailStore *store, QWidget *parent = nullptr);

    MessageModel *messages() const { return m_model; }
    void setFolder(const QString &account, const QByteArray &path, bool showRecipients);
    void setCategory(const QString &category) { m_model->setCategory(category); }
    void setSearch(const QString &text);
    // Beside the message: each message on two lines (sender and date, the subject under them), no columns.
    void setCards(bool cards);
    bool cards() const { return m_cards; }
    // The message that is selected, or none (uid 0).
    MailStore::Summary current() const;
    void selectUid(qint64 uid);

signals:
    void messageChosen(const MailStore::Summary &summary);
    void messageActivated(const MailStore::Summary &summary); // a double click: in a window of its own

private:
    MessageModel *m_model;
    QSortFilterProxyModel *m_filter;
    qint64 m_keptUid = 0;
    bool m_cards = false;
    class CardDelegate *m_cardDelegate = nullptr;
};
