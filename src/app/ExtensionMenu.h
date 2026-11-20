#pragma once

#include "Extensions.h"

#include <QJsonArray>

class QMenu;
class QWidget;

// Extensions' commands as menu entries (Argonaut's ExtensionMenu, for
// messages): their submenus, ticks, the question a command wants asked
// first, the text it wants typed, and the hand-over to the extension.
namespace ExtensionMenu {

// Adds the commands for the selected messages (`selection`, as sent to the
// extension) to `menu`. Returns how many were added.
int add(QMenu *menu, QWidget *window, const QJsonArray &selection);

// The commands extensions want in the menu bar's `which` menu ("file"…), at
// the end of `menu` after a separator. What an earlier call added is taken
// out first, so it can be called every time the menu opens.
void addToMenuBar(QMenu *menu, QWidget *window, const QString &which);

// Asks what `command` wants asked and typed, then tells its extension.
// False if the user backed out.
bool run(QWidget *window, const Extensions::Command &command, const QJsonArray &selection);

} // namespace ExtensionMenu
