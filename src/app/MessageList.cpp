#include "MessageList.h"

#include <QHeaderView>
#include <QLocale>

MessageModel::MessageModel(MailStore *store, QObject *parent)
    : QAbstractTableModel(parent)
    , m_store(store)
{
    connect(store, &MailStore::folderChanged, this, [this](const QString &account, const QByteArray &path) {
        if (account == m_account && path == m_folder)
            reload();
    });
}

void MessageModel::setFolder(const QString &account, const QByteArray &path, bool showRecipients)
{
    m_account = account;
    m_folder = path;
    m_recipients = showRecipients;
    reload();
    emit headerDataChanged(Qt::Horizontal, PeopleColumn, PeopleColumn);
}

void MessageModel::reload()
{
    beginResetModel();
    m_rows = m_account.isEmpty() ? QList<MailStore::Summary>() : m_store->messages(m_account, m_folder);
    endResetModel();
}

int MessageModel::rowOf(qint64 uid) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).uid == uid)
            return i;
    }
    return -1;
}

int MessageModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

int MessageModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

namespace {

QString shortDate(const QDateTime &date)
{
    if (!date.isValid())
        return {};
    const QDateTime local = date.toLocalTime();
    const QDate today = QDate::currentDate();
    if (local.date() == today)
        return QLocale().toString(local.time(), QLocale::ShortFormat);
    if (local.date().year() == today.year())
        return QLocale().toString(local.date(), QStringLiteral("d MMM"));
    return QLocale().toString(local.date(), QLocale::ShortFormat);
}

} // namespace

QVariant MessageModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const MailStore::Summary &s = m_rows.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case StarColumn:
            return s.flagged ? QStringLiteral("★") : QString();
        case PeopleColumn:
            if (m_recipients)
                return s.to;
            return s.fromName.isEmpty() ? s.fromEmail : s.fromName;
        case SubjectColumn:
            return s.subject.isEmpty() ? tr("(no subject)") : s.subject;
        case DateColumn:
            return shortDate(s.date);
        }
        break;
    case Qt::ToolTipRole:
        if (index.column() == PeopleColumn)
            return m_recipients ? s.to : (s.fromName.isEmpty() ? s.fromEmail : QStringLiteral("%1 <%2>").arg(s.fromName, s.fromEmail));
        if (index.column() == DateColumn && s.date.isValid())
            return QLocale().toString(s.date.toLocalTime(), QLocale::LongFormat);
        if (index.column() == SubjectColumn)
            return s.subject;
        break;
    case Qt::FontRole:
        if (!s.seen) {
            QFont bold;
            bold.setBold(true);
            return bold;
        }
        break;
    case Qt::TextAlignmentRole:
        if (index.column() == StarColumn)
            return int(Qt::AlignCenter);
        break;
    case SearchRole:
        return s.subject + QLatin1Char(' ') + s.fromName + QLatin1Char(' ') + s.fromEmail + QLatin1Char(' ') + s.to;
    case Qt::UserRole + 100: // the order: by date
        return s.date.isValid() ? s.date.toSecsSinceEpoch() : 0;
    }
    return {};
}

QVariant MessageModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case StarColumn:
        return QStringLiteral("★");
    case PeopleColumn:
        return m_recipients ? tr("To") : tr("From");
    case SubjectColumn:
        return tr("Subject");
    case DateColumn:
        return tr("Date");
    }
    return {};
}

MessageList::MessageList(MailStore *store, QWidget *parent)
    : QTreeView(parent)
    , m_model(new MessageModel(store, this))
    , m_filter(new QSortFilterProxyModel(this))
{
    setObjectName(QStringLiteral("messageList"));
    m_filter->setSourceModel(m_model);
    m_filter->setFilterRole(MessageModel::SearchRole);
    m_filter->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_filter->setFilterKeyColumn(0);
    setModel(m_filter);
    setRootIsDecorated(false);
    setUniformRowHeights(true);
    setAllColumnsShowFocus(true);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setFrameShape(QFrame::NoFrame);
    header()->setStretchLastSection(false);
    header()->setSectionResizeMode(MessageModel::StarColumn, QHeaderView::Fixed);
    header()->resizeSection(MessageModel::StarColumn, 28);
    header()->setSectionResizeMode(MessageModel::PeopleColumn, QHeaderView::Interactive);
    header()->resizeSection(MessageModel::PeopleColumn, 220);
    header()->setSectionResizeMode(MessageModel::SubjectColumn, QHeaderView::Stretch);
    header()->setSectionResizeMode(MessageModel::DateColumn, QHeaderView::ResizeToContents);

    // A list that is made again (new mail, a flag) keeps the message it was on.
    connect(m_model, &QAbstractItemModel::modelAboutToBeReset, this, [this] { m_keptUid = current().uid; });
    connect(m_model, &QAbstractItemModel::modelReset, this, [this] {
        if (m_keptUid == 0)
            return;
        const int row = m_model->rowOf(m_keptUid);
        if (row < 0)
            return;
        const QModelIndex index = m_filter->mapFromSource(m_model->index(row, 0));
        QSignalBlocker block(selectionModel());
        setCurrentIndex(index);
    });
    connect(selectionModel(), &QItemSelectionModel::currentRowChanged, this, [this](const QModelIndex &index) {
        if (index.isValid())
            emit messageChosen(m_model->summaryAt(m_filter->mapToSource(index).row()));
    });
    connect(this, &QTreeView::activated, this, [this](const QModelIndex &index) {
        emit messageActivated(m_model->summaryAt(m_filter->mapToSource(index).row()));
    });
}

void MessageList::setFolder(const QString &account, const QByteArray &path, bool showRecipients)
{
    m_model->setFolder(account, path, showRecipients);
}

void MessageList::setSearch(const QString &text)
{
    m_filter->setFilterFixedString(text);
}

MailStore::Summary MessageList::current() const
{
    const QModelIndex index = currentIndex();
    if (!index.isValid())
        return {};
    return m_model->summaryAt(m_filter->mapToSource(index).row());
}

void MessageList::selectUid(qint64 uid)
{
    const int row = m_model->rowOf(uid);
    if (row >= 0)
        setCurrentIndex(m_filter->mapFromSource(m_model->index(row, 0)));
}
