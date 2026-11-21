#include "FlowLayout.h"

#include <QWidget>

FlowLayout::FlowLayout(QWidget *parent, int spacing)
    : QLayout(parent)
    , m_spacing(spacing)
{
}

FlowLayout::~FlowLayout()
{
    while (QLayoutItem *item = takeAt(0))
        delete item;
}

void FlowLayout::addItem(QLayoutItem *item)
{
    m_items.append(item);
}

QLayoutItem *FlowLayout::takeAt(int index)
{
    return index >= 0 && index < m_items.size() ? m_items.takeAt(index) : nullptr;
}

void FlowLayout::setGeometry(const QRect &rect)
{
    QLayout::setGeometry(rect);
    arrange(rect, true);
}

QSize FlowLayout::minimumSize() const
{
    QSize size;
    for (const QLayoutItem *item : m_items)
        size = size.expandedTo(item->minimumSize());
    const QMargins m = contentsMargins();
    return size + QSize(m.left() + m.right(), m.top() + m.bottom());
}

int FlowLayout::arrange(const QRect &rect, bool apply) const
{
    const QMargins m = contentsMargins();
    const QRect room = rect.adjusted(m.left(), m.top(), -m.right(), -m.bottom());
    int x = room.x();
    int y = room.y();
    int lineHeight = 0;
    for (QLayoutItem *item : m_items) {
        const QSize hint = item->sizeHint().boundedTo(QSize(room.width(), QWIDGETSIZE_MAX));
        if (x > room.x() && x + hint.width() > room.right() + 1) { // the next line
            x = room.x();
            y += lineHeight + m_spacing;
            lineHeight = 0;
        }
        if (apply)
            item->setGeometry(QRect(QPoint(x, y), hint));
        x += hint.width() + m_spacing;
        lineHeight = qMax(lineHeight, hint.height());
    }
    return y + lineHeight - rect.y() + m.bottom();
}
