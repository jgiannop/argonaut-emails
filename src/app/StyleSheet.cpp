#include "StyleSheet.h"

QString styleSheetFor(const Theme &t)
{
    QString css = QStringLiteral(R"(
QMainWindow, QWidget#messageView, QTreeView#messageList { background: @window; color: @fg; }
QTreeWidget#folderTree { background: @sidebar; color: @fg; border: none; padding: 6px 4px; outline: 0; show-decoration-selected: 0; }
QTreeWidget#folderTree::branch, QTreeWidget#folderTree::branch:selected { background: @sidebar; border: none; }
QTreeWidget#folderTree::item { padding: 4px 4px; border-radius: 6px; }
QTreeWidget#folderTree::item:selected { background: @selected; color: @fg; }
QTreeWidget#folderTree::item:hover:!selected { background: @hover; }
QTreeView#messageList { border: none; outline: 0; }
QTreeView#messageList::item { padding: 3px 4px; }
QTreeView#messageList::item:selected { background: @itemSelected; color: @fg; }
QTreeView#messageList::item:hover:!selected { background: @hover; }
QHeaderView::section { background: @window; color: @dimFg; border: none; border-bottom: 1px solid @border; padding: 4px 6px; }
QWidget#messageHeader { background: @sidebar; border-bottom: 1px solid @border; }
QWidget#messageNoteBar { background: @pathBg; }
QLabel#messageNote { color: @dimFg; }
QWidget#messageFiles { background: @sidebar; border-top: 1px solid @border; }
QLineEdit#searchField { background: @pathBg; border: none; border-radius: 8px; padding: 6px 8px; margin: 6px; color: @fg; }
QTabBar#categoryTabs { background: @window; border: none; }
QTabBar#categoryTabs::tab { background: transparent; color: @dimFg; border: none; border-bottom: 3px solid transparent; padding: 6px 16px 4px 16px; margin: 0 2px; }
QTabBar#categoryTabs::tab:selected { color: @fg; border-bottom: 3px solid @accent; }
QTabBar#categoryTabs::tab:hover:!selected { background: @hover; }
QToolBar { background: @sidebar; border: none; border-bottom: 1px solid @border; spacing: 4px; padding: 4px; }
QToolButton { border-radius: 6px; padding: 4px 8px; color: @fg; }
QToolButton:hover { background: @hover; }
QToolButton:pressed { background: @pressed; }
QSplitter::handle { background: @border; }
QSplitter::handle:horizontal { width: 1px; }
QSplitter::handle:vertical { height: 1px; }
QStatusBar { background: @sidebar; color: @dimFg; }
QMenu { background: @popup; color: @fg; border: 1px solid @border; padding: 4px; }
QMenu::item { padding: 5px 20px; border-radius: 4px; }
QMenu::item:selected { background: @hover; }
QMenu::item:disabled { color: @dimFg; }
QMenuBar { background: @sidebar; color: @fg; }
QMenuBar::item:selected { background: @hover; }
QScrollBar:vertical { background: transparent; width: 10px; }
QScrollBar::handle:vertical { background: @scroll; border-radius: 4px; min-height: 30px; margin: 2px; }
QScrollBar::handle:vertical:hover { background: @scrollHover; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QFrame#commandPalette { background: @popup; border: 1px solid @border; border-radius: 12px; }
QLineEdit#paletteEdit { background: @pathBg; color: @fg; border: none; border-radius: 8px;
    padding: 8px 10px; font-size: 14px; selection-background-color: @accent; }
QListWidget#paletteList { background: transparent; color: @fg; outline: 0; }
QDialog#extensionsDialog, QDialog#claudeDialog, QDialog#connectDialog { background: @window; }
QFrame#extCard { background: @pathBg; border-radius: 10px; }
QFrame#extCard QLabel#prefHint { background: transparent; }
QLabel#extName { color: @fg; font-weight: bold; background: transparent; }
QLabel#extState { color: @dimFg; background: transparent; }
QLabel#extState[look="good"] { color: @accent; }
QLabel#extState[look="bad"] { color: #e01b24; }
QScrollArea#prefScroll, QWidget#prefContent { background: @window; }
QLabel#prefHeading { color: @fg; font-weight: bold; font-size: 15px; }
QLabel#prefSubheading { color: @fg; font-weight: bold; }
QLabel#prefHint { color: @dimFg; }
QLabel#propDanger { color: #e01b24; }
QCheckBox#prefCheck { color: @fg; spacing: 8px; }
QCheckBox#prefCheck::indicator { width: 14px; height: 14px; border: 1px solid @dimFg;
    border-radius: 4px; background: @pathBg; }
QCheckBox#prefCheck::indicator:checked { background: @accent; border-color: @accent; }
QLineEdit#connectAddress, QLineEdit#claudeKey { background: @pathBg; color: @fg; border: none; border-radius: 6px;
    padding: 7px 9px; selection-background-color: @accent; }
QPushButton#prefButton { background: @pathBg; color: @fg; border: none; border-radius: 6px; padding: 7px 16px; }
QPushButton#prefButton:hover { background: @hover; }
QPushButton#prefButton:disabled { color: @dimFg; }
)");
    const QList<QPair<QString, QColor>> tokens{
        {QStringLiteral("@window"), t.window},   {QStringLiteral("@sidebar"), t.sidebar},
        {QStringLiteral("@fg"), t.fg},           {QStringLiteral("@dimFg"), t.dimFg},
        {QStringLiteral("@hover"), t.hover},     {QStringLiteral("@pressed"), t.pressed},
        {QStringLiteral("@selected"), t.selected}, {QStringLiteral("@itemSelected"), t.itemSelected},
        {QStringLiteral("@pathBg"), t.pathBg},   {QStringLiteral("@popup"), t.popup},
        {QStringLiteral("@border"), t.border},   {QStringLiteral("@scrollHover"), t.scrollHover},
        {QStringLiteral("@scroll"), t.scroll},   {QStringLiteral("@accent"), t.accent},
    };
    // Longer names first: @scrollHover before @scroll, @itemSelected before @selected.
    for (const auto &token : tokens)
        css.replace(token.first, cssColor(token.second));
    return css;
}
