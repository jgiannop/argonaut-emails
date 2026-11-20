#include "MessageList.h"

#include <QApplication>
#include <QHeaderView>
#include <QLocale>
#include <QPainter>
#include <QStyledItemDelegate>

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

void MessageModel::setCategory(const QString &category)
{
    if (category == m_category)
        return;
    m_category = category;
    reload();
}

void MessageModel::reload()
{
    beginResetModel();
    m_rows = m_account.isEmpty() ? QList<MailStore::Summary>() : m_store->messages(m_account, m_folder, 5000, m_category);
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

// One message on two lines, for the list beside the message:
//   Sender                          ★  12:29
//   The subject, as wide as the list
// The other columns are hidden; their text is read from the same row.
class CardDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &) const override
    {
        const QFontMetrics fm(option.font);
        return {200, fm.height() * 2 + 4 + 2 * Padding};
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        opt.text.clear(); // the background and the selection as the theme draws them; the text is ours
        const QWidget *widget = option.widget;
        QStyle *style = widget ? widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);

        const auto text = [&index](int column) { return index.siblingAtColumn(column).data().toString(); };
        const bool unread = index.data(Qt::FontRole).value<QFont>().bold();
        // The theme's selection is a light tint under the usual text colour (StyleSheet: ::item:selected).
        const QColor color = opt.palette.color(QPalette::Text);
        QColor dim = color;
        dim.setAlphaF(0.6);

        painter->save();
        const QRect r = option.rect.adjusted(Padding + 6, Padding, -Padding - 4, -Padding);
        QFont font = option.font;
        font.setBold(unread);
        const QFontMetrics fm(font);
        const int line = fm.height();

        // The first line: the date (and the star) at the right, the sender in what is left.
        QString right = text(MessageModel::DateColumn);
        if (!text(MessageModel::StarColumn).isEmpty())
            right = QStringLiteral("★  ") + right;
        painter->setFont(option.font);
        painter->setPen(dim);
        const int rightWidth = QFontMetrics(option.font).horizontalAdvance(right);
        painter->drawText(QRect(r.right() - rightWidth, r.top(), rightWidth, line), Qt::AlignRight | Qt::AlignVCenter, right);
        painter->setFont(font);
        painter->setPen(color);
        const QRect who(r.left(), r.top(), r.width() - rightWidth - 12, line);
        painter->drawText(who, Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(text(MessageModel::PeopleColumn), Qt::ElideRight, who.width()));
        // The second: the subject.
        const QRect subject(r.left(), r.top() + line + 2, r.width(), line);
        painter->setPen(unread ? color : dim);
        painter->drawText(subject, Qt::AlignLeft | Qt::AlignVCenter,
                          fm.elidedText(text(MessageModel::SubjectColumn), Qt::ElideRight, subject.width()));
        // A thin line under it.
        QColor rule = color;
        rule.setAlphaF(0.08);
        painter->setPen(rule);
        painter->drawLine(option.rect.left(), option.rect.bottom(), option.rect.right(), option.rect.bottom());
        painter->restore();
    }

private:
    static constexpr int Padding = 6;
};

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

void MessageList::setCards(bool cards)
{
    if (cards == m_cards)
        return;
    m_cards = cards;
    if (!m_cardDelegate)
        m_cardDelegate = new CardDelegate(this);
    setItemDelegateForColumn(MessageModel::PeopleColumn, cards ? m_cardDelegate : nullptr);
    header()->setVisible(!cards);
    for (const int column : {MessageModel::StarColumn, MessageModel::SubjectColumn, MessageModel::DateColumn})
        setColumnHidden(column, cards);
    header()->setSectionResizeMode(MessageModel::PeopleColumn, cards ? QHeaderView::Stretch : QHeaderView::Interactive);
    if (!cards)
        header()->resizeSection(MessageModel::PeopleColumn, 220);
    doItemsLayout(); // the rows' height again
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
