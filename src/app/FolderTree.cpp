#include "FolderTree.h"

#include "Account.h"
#include "ImapParser.h"

#include <QHeaderView>

namespace {

enum Role { AccountRole = Qt::UserRole, PathRole, NameRole, CountRole };

Glyph glyphFor(const QString &role)
{
    if (role == QLatin1String("inbox"))
        return Glyph::Downloads;
    if (role == QLatin1String("trash"))
        return Glyph::Trash;
    if (role == QLatin1String("sent"))
        return Glyph::ChevronRight;
    if (role == QLatin1String("drafts"))
        return Glyph::Documents;
    if (role == QLatin1String("archive"))
        return Glyph::Drive;
    if (role == QLatin1String("junk"))
        return Glyph::Close;
    return Glyph::Folder;
}

} // namespace

FolderTree::FolderTree(MailStore *store, QWidget *parent)
    : QTreeWidget(parent)
    , m_store(store)
{
    setObjectName(QStringLiteral("folderTree"));
    setHeaderHidden(true);
    setColumnCount(1);
    setIndentation(14);
    setUniformRowHeights(true);
    setFrameShape(QFrame::NoFrame);
    setIconSize(QSize(16, 16));
    connect(this, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) {
        if (m_reloading || !item)
            return;
        const QByteArray path = item->data(0, PathRole).toByteArray();
        if (!path.isEmpty())
            emit folderChosen(item->data(0, AccountRole).toString(), path);
        else
            emit accountChosen(item->data(0, AccountRole).toString());
    });
    connect(store, &MailStore::foldersChanged, this, [this] { reload(); });
    connect(store, &MailStore::folderChanged, this, [this](const QString &account) { updateCounts(account); });
}

void FolderTree::reload()
{
    const QString account = currentAccount();
    const QByteArray folder = currentFolder();
    m_reloading = true;
    clear();
    for (const Account &a : Account::all()) {
        auto *top = new QTreeWidgetItem(this);
        top->setText(0, a.title());
        top->setData(0, AccountRole, a.id);
        top->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable); // to reach its settings, folders or not
        QFont bold = font();
        bold.setBold(true);
        top->setFont(0, bold);
        QHash<QString, QTreeWidgetItem *> byName; // decoded path → row
        for (const MailStore::Folder &f : m_store->folders(a.id)) {
            const QString decoded = Imap::decodeMailboxName(f.path);
            const QString parentName = decoded.section(QLatin1Char(f.delimiter), 0, -2);
            QTreeWidgetItem *parent = parentName.isEmpty() ? top : byName.value(parentName, top);
            auto *item = new QTreeWidgetItem(parent);
            item->setData(0, AccountRole, a.id);
            item->setData(0, NameRole, f.name);
            item->setData(0, Qt::UserRole + 10, f.role);
            if (f.selectable)
                item->setData(0, PathRole, f.path);
            else
                item->setFlags(Qt::ItemIsEnabled);
            item->setToolTip(0, decoded);
            byName.insert(decoded, item);
        }
        top->setExpanded(true);
    }
    for (int i = 0; i < topLevelItemCount(); ++i)
        updateCounts(topLevelItem(i)->data(0, AccountRole).toString());
    applyTheme(m_theme);
    m_reloading = false;
    if (!account.isEmpty() && !folder.isEmpty()) {
        select(account, folder);
        return;
    }
    // Nothing chosen yet: the first account's inbox, as soon as there is one.
    for (int i = 0; i < topLevelItemCount(); ++i) {
        const QString id = topLevelItem(i)->data(0, AccountRole).toString();
        for (const MailStore::Folder &f : m_store->folders(id)) {
            if (f.role == QLatin1String("inbox") && f.selectable) {
                select(id, f.path);
                return;
            }
        }
    }
}

void FolderTree::updateCounts(const QString &account)
{
    QHash<QByteArray, MailStore::Folder> folders;
    for (const MailStore::Folder &f : m_store->folders(account))
        folders.insert(f.path, f);
    QTreeWidgetItemIterator it(this);
    while (*it) {
        QTreeWidgetItem *item = *it;
        ++it;
        if (item->data(0, AccountRole).toString() != account || item->data(0, NameRole).toString().isEmpty())
            continue;
        const MailStore::Folder f = folders.value(item->data(0, PathRole).toByteArray());
        const QString name = item->data(0, NameRole).toString();
        const bool showCount = f.unread > 0 && f.role != QLatin1String("trash") && f.role != QLatin1String("junk") && f.role != QLatin1String("sent");
        item->setText(0, showCount ? QStringLiteral("%1  (%2)").arg(name).arg(f.unread) : name);
        QFont font = this->font();
        font.setBold(showCount);
        item->setFont(0, font);
    }
}

void FolderTree::applyTheme(const Theme &theme)
{
    m_theme = theme;
    QTreeWidgetItemIterator it(this);
    while (*it) {
        QTreeWidgetItem *item = *it;
        ++it;
        if (item->data(0, NameRole).toString().isEmpty())
            item->setIcon(0, glyphIcon(Glyph::Home, theme.dimFg));
        else
            item->setIcon(0, glyphIcon(glyphFor(item->data(0, Qt::UserRole + 10).toString()), theme.dimFg));
    }
}

void FolderTree::select(const QString &account, const QByteArray &path)
{
    QTreeWidgetItemIterator it(this);
    while (*it) {
        if ((*it)->data(0, AccountRole).toString() == account && (*it)->data(0, PathRole).toByteArray() == path) {
            setCurrentItem(*it);
            return;
        }
        ++it;
    }
}

QString FolderTree::currentAccount() const
{
    return currentItem() ? currentItem()->data(0, AccountRole).toString() : QString();
}

QByteArray FolderTree::currentFolder() const
{
    return currentItem() ? currentItem()->data(0, PathRole).toByteArray() : QByteArray();
}
