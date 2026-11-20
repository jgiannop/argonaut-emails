#include "RecentPopup.h"

#include "Account.h"

#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScreen>
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include <algorithm>

namespace {

enum Role { SummaryRole = Qt::UserRole + 1 };
constexpr int RowHeight = 54;

// "14:02" today, "Tue" this week, "12 Nov" this year, else "12/11/2025".
QString when(const QDateTime &date)
{
    const QDateTime local = date.toLocalTime();
    const QDate today = QDate::currentDate();
    const QLocale locale;
    if (local.date() == today)
        return locale.toString(local.time(), QLocale::ShortFormat);
    if (local.date().daysTo(today) < 7)
        return locale.toString(local.date(), QStringLiteral("ddd"));
    if (local.date().year() == today.year())
        return locale.toString(local.date(), QStringLiteral("d MMM"));
    return locale.toString(local.date(), QLocale::ShortFormat);
}

// Two lines a message, as in a phone's mail: a dot and the sender in bold
// while unread, the date at the right; the subject under them.
class RowDelegate : public QStyledItemDelegate
{
public:
    explicit RowDelegate(const Theme *theme, QObject *parent)
        : QStyledItemDelegate(parent)
        , m_theme(theme)
    {
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &) const override
    {
        return {option.rect.width(), RowHeight};
    }

    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const auto s = index.data(SummaryRole).value<MailStore::Summary>();
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const QRect r = option.rect.adjusted(4, 1, -4, -1);
        if (option.state & (QStyle::State_Selected | QStyle::State_MouseOver)) {
            QPainterPath bg;
            bg.addRoundedRect(r, 6, 6);
            p->fillPath(bg, (option.state & QStyle::State_Selected) ? m_theme->itemSelected : m_theme->hover);
        }
        const QRect text = r.adjusted(22, 6, -10, -6);
        if (!s.seen) {
            p->setPen(Qt::NoPen);
            p->setBrush(m_theme->accent);
            p->drawEllipse(QPointF(r.left() + 11, text.top() + 8), 4, 4);
        }
        QFont bold = option.font;
        bold.setBold(!s.seen);
        const QFontMetrics boldMetrics(bold);
        const QFontMetrics plain(option.font);
        const QString date = when(s.date);
        const int dateWidth = plain.horizontalAdvance(date) + 8;
        const QRect top(text.left(), text.top(), text.width(), boldMetrics.height());
        const QRect bottom(text.left(), top.bottom() + 4, text.width(), plain.height());
        QString who = s.fromName.isEmpty() ? s.fromEmail : s.fromName;
        p->setFont(bold);
        p->setPen(m_theme->fg);
        p->drawText(top.adjusted(0, 0, -dateWidth, 0), Qt::AlignLeft | Qt::AlignVCenter,
                    boldMetrics.elidedText(who, Qt::ElideRight, top.width() - dateWidth));
        p->setFont(option.font);
        p->setPen(s.seen ? m_theme->dimFg : m_theme->accent);
        p->drawText(top, Qt::AlignRight | Qt::AlignVCenter, date);
        p->setPen(s.seen ? m_theme->dimFg : m_theme->fg);
        const QString subject = s.subject.isEmpty() ? RecentPopup::tr("(no subject)") : s.subject;
        p->drawText(bottom, Qt::AlignLeft | Qt::AlignVCenter, plain.elidedText(subject, Qt::ElideRight, bottom.width()));
        p->restore();
    }

private:
    const Theme *m_theme;
};

// An account's inbox and, on Gmail, its Primary tab ("primary"), else all of it.
QList<QPair<QString, QPair<QByteArray, QString>>> inboxes(MailStore *store)
{
    QList<QPair<QString, QPair<QByteArray, QString>>> out;
    for (const Account &account : Account::all()) {
        for (const MailStore::Folder &folder : store->folders(account.id)) {
            if (folder.role != QLatin1String("inbox"))
                continue;
            const bool tabs = !store->categoryCounts(account.id, folder.path).isEmpty();
            out.append({account.id, {folder.path, tabs ? QStringLiteral("primary") : QString()}});
        }
    }
    return out;
}

} // namespace


RecentPopup::RecentPopup(MailStore *store, QWidget *parent)
    : QFrame(parent, Qt::Popup)
    , m_store(store)
    , m_theme(currentTheme())
{
    setObjectName(QStringLiteral("recentPopup"));
    setAttribute(Qt::WA_TranslucentBackground, false);
    resize(380, 470);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(6);

    auto *top = new QHBoxLayout;
    m_title = new QLabel(tr("Inbox"), this);
    m_title->setObjectName(QStringLiteral("recentTitle"));
    m_count = new QLabel(this);
    m_count->setObjectName(QStringLiteral("recentCount"));
    auto *open = new QPushButton(tr("Open Argonaut Emails"), this);
    open->setObjectName(QStringLiteral("prefButton"));
    open->setCursor(Qt::PointingHandCursor);
    connect(open, &QPushButton::clicked, this, [this] {
        hide();
        emit openRequested();
    });
    top->addWidget(m_title);
    top->addWidget(m_count);
    top->addStretch(1);
    top->addWidget(open);
    layout->addLayout(top);

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("recentList"));
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setUniformItemSizes(true);
    m_list->setMouseTracking(true);
    m_list->setItemDelegate(new RowDelegate(&m_theme, m_list));
    layout->addWidget(m_list, 1);

    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        const auto s = item->data(SummaryRole).value<MailStore::Summary>();
        if (s.uid == 0)
            return;
        hide();
        emit messageChosen(s);
    });
    // At the end of the list: ten more.
    connect(m_list->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        if (m_more && value >= m_list->verticalScrollBar()->maximum() - RowHeight / 2 && m_wanted < Most)
            fill(m_wanted + Step);
    });
}

QList<MailStore::Summary> RecentPopup::newest(MailStore *store, int count)
{
    QList<MailStore::Summary> all;
    for (const auto &inbox : inboxes(store))
        all += store->messages(inbox.first, inbox.second.first, count, inbox.second.second);
    std::stable_sort(all.begin(), all.end(), [](const MailStore::Summary &a, const MailStore::Summary &b) { return a.date > b.date; });
    if (all.size() > count)
        all.resize(count);
    return all;
}

int RecentPopup::unread(MailStore *store)
{
    int n = 0;
    for (const auto &inbox : inboxes(store)) {
        if (inbox.second.second.isEmpty())
            n += store->folder(inbox.first, inbox.second.first).unread;
        else
            n += store->categoryCounts(inbox.first, inbox.second.first).value(inbox.second.second).second;
    }
    return n;
}

int RecentPopup::shownCount() const
{
    int n = 0;
    for (int i = 0; i < m_list->count(); ++i)
        n += m_list->item(i)->data(SummaryRole).isValid() ? 1 : 0;
    return n;
}

void RecentPopup::fill(int count)
{
    const int scrolled = m_list->verticalScrollBar()->value();
    m_wanted = count;
    const QList<MailStore::Summary> messages = newest(m_store, count);
    m_more = messages.size() >= count;
    m_list->clear();
    for (const MailStore::Summary &s : messages) {
        auto *item = new QListWidgetItem(m_list);
        item->setData(SummaryRole, QVariant::fromValue(s));
        item->setToolTip(QStringLiteral("%1\n%2").arg(s.fromEmail, s.subject));
    }
    if (messages.isEmpty()) {
        auto *none = new QListWidgetItem(tr("No messages in your inbox"), m_list);
        none->setFlags(Qt::NoItemFlags);
        none->setTextAlignment(Qt::AlignCenter);
    }
    m_list->doItemsLayout(); // the new length now, not later: the scroll range must know it
    m_list->verticalScrollBar()->setValue(scrolled);
    const int unread = RecentPopup::unread(m_store);
    m_count->setText(unread == 0 ? QString() : tr("%n unread", nullptr, unread));
}

void RecentPopup::reload()
{
    if (isVisible())
        fill(m_wanted);
}

void RecentPopup::applyTheme(const Theme &theme)
{
    m_theme = theme;
    m_list->viewport()->update();
}

void RecentPopup::popup(const QRect &anchor)
{
    m_wanted = Step;
    fill(Step);
    m_list->scrollToTop();
    // Under the icon (a top bar) or over it (a bottom panel), inside its screen.
    const QPoint at = anchor.isValid() ? anchor.center() : QCursor::pos();
    QScreen *screen = QGuiApplication::screenAt(at);
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    const QRect room = screen->availableGeometry();
    QPoint pos(at.x() - width() / 2, (anchor.isValid() ? anchor.bottom() : at.y()) + 6);
    if (pos.y() + height() > room.bottom())
        pos.setY((anchor.isValid() ? anchor.top() : at.y()) - height() - 6);
    pos.setX(qBound(room.left() + 4, pos.x(), room.right() - width() - 4));
    pos.setY(qBound(room.top() + 4, pos.y(), room.bottom() - height() - 4));
    move(pos);
    show();
    raise();
    activateWindow();
    m_list->setFocus();
}
