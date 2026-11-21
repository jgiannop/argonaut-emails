#pragma once

#include <QLayout>
#include <QList>

// Widgets side by side, onto the next line when the room ends, as words in
// a paragraph (Qt's own example, made small). Its height follows its width.
class FlowLayout : public QLayout
{
public:
    explicit FlowLayout(QWidget *parent = nullptr, int spacing = 6);
    ~FlowLayout() override;

    void addItem(QLayoutItem *item) override;
    int count() const override { return int(m_items.size()); }
    QLayoutItem *itemAt(int index) const override { return m_items.value(index); }
    QLayoutItem *takeAt(int index) override;
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return arrange(QRect(0, 0, width, 0), false); }
    void setGeometry(const QRect &rect) override;
    QSize sizeHint() const override { return minimumSize(); }
    QSize minimumSize() const override; // the widest item: never wider than that

private:
    int arrange(const QRect &rect, bool apply) const;

    QList<QLayoutItem *> m_items;
    int m_spacing;
};
