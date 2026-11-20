#include "CommandPalette.h"

#include "Theme.h"

#include <QAction>
#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QSettings>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace {

// Letters to compare: lower case, accents off ("é" finds "e").
QString fold(const QString &text)
{
    const QString decomposed = text.normalized(QString::NormalizationForm_D);
    QString out;
    out.reserve(decomposed.size());
    for (const QChar c : decomposed) {
        if (c.category() != QChar::Mark_NonSpacing)
            out.append(c);
    }
    return out.toCaseFolded();
}

enum Role { WhereRole = Qt::UserRole + 1, KeysRole, IndexRole };

constexpr int MaxRows = 9;
constexpr int RowHeight = 34;

// One row: the command, where it lives (dim), and its shortcut on the right.
class CommandDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &) const override
    {
        return QSize(option.rect.width(), RowHeight);
    }

    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const Theme t = currentTheme();
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const QRect r = option.rect.adjusted(4, 1, -4, -1);
        if (option.state & QStyle::State_Selected) {
            QPainterPath bg;
            bg.addRoundedRect(r, 6, 6);
            p->fillPath(bg, t.itemSelected);
        }
        QFont font = option.font;
        const QFontMetrics fm(font);
        const QString keys = index.data(KeysRole).toString();
        const int keysWidth = keys.isEmpty() ? 0 : fm.horizontalAdvance(keys) + 16;
        QRect text = r.adjusted(10, 0, -10 - keysWidth, 0);

        const QString name = index.data().toString();
        const QString where = index.data(WhereRole).toString();
        p->setPen(t.fg);
        p->setFont(font);
        const QString shownName = fm.elidedText(name, Qt::ElideRight, text.width());
        p->drawText(text, Qt::AlignLeft | Qt::AlignVCenter, shownName);
        const int used = fm.horizontalAdvance(shownName) + 10;
        if (!where.isEmpty() && used < text.width()) {
            p->setPen(t.dimFg);
            p->drawText(text.adjusted(used, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter,
                        fm.elidedText(where, Qt::ElideRight, text.width() - used));
        }
        if (!keys.isEmpty()) {
            const QRect keyRect(r.right() - keysWidth - 6, r.center().y() - fm.height() / 2 - 2, keysWidth,
                                fm.height() + 4);
            QPainterPath cap;
            cap.addRoundedRect(keyRect, 5, 5);
            p->fillPath(cap, t.pathBg);
            p->setPen(t.dimFg);
            p->drawText(keyRect, Qt::AlignCenter, keys);
        }
        p->restore();
    }
};

} // namespace

CommandPalette::CommandPalette(QWidget *parent) : QFrame(parent)
{
    setObjectName("commandPalette");
    setFrameShape(QFrame::NoFrame);
    hide();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);
    m_edit = new QLineEdit(this);
    m_edit->setObjectName("paletteEdit");
    m_edit->setPlaceholderText(tr("Type a command"));
    m_edit->installEventFilter(this);
    layout->addWidget(m_edit);
    m_list = new QListWidget(this);
    m_list->setObjectName("paletteList");
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setFocusPolicy(Qt::NoFocus);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setUniformItemSizes(true);
    m_list->setItemDelegate(new CommandDelegate(m_list));
    layout->addWidget(m_list);

    connect(m_edit, &QLineEdit::textChanged, this, &CommandPalette::filter);
    connect(m_list, &QListWidget::itemClicked, this, [this] { runCurrent(); });
    parent->installEventFilter(this); // follow the window's size
}

void CommandPalette::open(const QList<Command> &commands)
{
    m_commands = commands;
    m_recent = QSettings().value(QStringLiteral("palette/recent")).toStringList();
    m_returnFocus = QApplication::focusWidget();
    m_edit->clear();
    filter();
    place();
    show();
    raise();
    m_edit->setFocus(Qt::PopupFocusReason);
}

void CommandPalette::close()
{
    if (!isVisible())
        return;
    hide();
    if (m_returnFocus)
        m_returnFocus->setFocus();
}

int CommandPalette::score(const QString &text, const QString &query)
{
    // Argonaut's rule (Naming::commandScore there).
    if (query.isEmpty())
        return 1;
    if (text.startsWith(query))
        return 1000;
    const qsizetype at = text.indexOf(query);
    if (at >= 0) {
        const bool wordStart = at == 0 || !text.at(at - 1).isLetterOrNumber();
        return (wordStart ? 800 : 500) - int(qMin<qsizetype>(at, 200));
    }
    // Every word somewhere.
    const QStringList words = query.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (words.size() > 1) {
        bool all = true;
        for (const QString &w : words)
            all = all && text.contains(w);
        if (all)
            return 300;
    }
    // Letters in order, preferring the start of words ("wn" -> "Write New").
    int score = 0;
    qsizetype pos = 0;
    for (const QChar c : query) {
        if (c == QLatin1Char(' '))
            continue;
        const qsizetype found = text.indexOf(c, pos);
        if (found < 0)
            return 0;
        score += (found == 0 || !text.at(found - 1).isLetterOrNumber()) ? 10 : 1;
        pos = found + 1;
    }
    return 100 + qMin(score, 99);
}

void CommandPalette::filter()
{
    const QString query = fold(m_edit->text().simplified());
    struct Hit
    {
        int index;
        int score;
        int recent;
    };
    QList<Hit> hits;
    for (int i = 0; i < m_commands.size(); ++i) {
        const Command &c = m_commands[i];
        // The name counts most; where it lives helps ("sort name").
        const QString name = fold(c.name);
        int s = score(name, query);
        if (s == 0)
            s = score(name + QLatin1Char(' ') + fold(c.where), query) / 2;
        if (s == 0)
            continue;
        const qsizetype r = m_recent.indexOf(c.where + QLatin1Char('/') + c.name);
        hits.append({i, s, r < 0 ? 1000 : int(r)});
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) {
        if (a.score != b.score)
            return a.score > b.score;
        return a.recent < b.recent;
    });

    m_list->clear();
    for (const Hit &h : std::as_const(hits)) {
        const Command &c = m_commands[h.index];
        auto *item = new QListWidgetItem(c.name, m_list);
        item->setData(WhereRole, c.where);
        item->setData(KeysRole, c.action->shortcut().toString(QKeySequence::NativeText));
        item->setData(IndexRole, h.index);
        if (c.action->isCheckable())
            item->setText(c.name + (c.action->isChecked() ? QStringLiteral("  ✓") : QString()));
    }
    if (m_list->count() > 0)
        m_list->setCurrentRow(0);
    else
        new QListWidgetItem(tr("No matching command"), m_list); // not selectable below
    if (m_list->count() == 1 && m_list->item(0)->data(IndexRole).isNull())
        m_list->item(0)->setFlags(Qt::NoItemFlags);
    place();
}

void CommandPalette::runCurrent()
{
    QListWidgetItem *item = m_list->currentItem();
    if (!item || item->data(IndexRole).isNull())
        return;
    const Command c = m_commands.value(item->data(IndexRole).toInt());
    if (!c.action)
        return;
    const QString key = c.where + QLatin1Char('/') + c.name;
    m_recent.removeAll(key);
    m_recent.prepend(key);
    while (m_recent.size() > 12)
        m_recent.removeLast();
    QSettings().setValue(QStringLiteral("palette/recent"), m_recent);
    close();
    // After the focus is back where it was, so the command acts there.
    QTimer::singleShot(0, c.action, [action = QPointer<QAction>(c.action)] {
        if (action && action->isEnabled())
            action->trigger();
    });
}

void CommandPalette::place()
{
    QWidget *window = parentWidget();
    const int width = qMin(620, window->width() - 40);
    const int rows = qBound(1, m_list->count(), MaxRows);
    m_list->setFixedHeight(rows * RowHeight + 4);
    adjustSize();
    resize(width, sizeHint().height());
    move((window->width() - width) / 2, qMin(64, window->height() / 8));
}

bool CommandPalette::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize && isVisible())
        place();
    if (watched == m_edit && event->type() == QEvent::FocusOut && isVisible()) {
        // Clicking elsewhere closes it (but not a click on the list itself).
        QTimer::singleShot(0, this, [this] {
            if (isVisible() && !m_edit->hasFocus() && !underMouse())
                close();
        });
    }
    if (watched == m_edit && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        const int row = m_list->currentRow();
        const int count = m_list->count();
        switch (key->key()) {
        case Qt::Key_Escape:
            close();
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            runCurrent();
            return true;
        case Qt::Key_Down:
            if (count > 0)
                m_list->setCurrentRow((row + 1) % count);
            return true;
        case Qt::Key_Up:
            if (count > 0)
                m_list->setCurrentRow((row - 1 + count) % count);
            return true;
        case Qt::Key_PageDown:
            if (count > 0)
                m_list->setCurrentRow(qMin(count - 1, row + MaxRows));
            return true;
        case Qt::Key_PageUp:
            if (count > 0)
                m_list->setCurrentRow(qMax(0, row - MaxRows));
            return true;
        default:
            break;
        }
    }
    return QFrame::eventFilter(watched, event);
}
