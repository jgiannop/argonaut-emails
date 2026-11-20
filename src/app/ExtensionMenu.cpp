#include "ExtensionMenu.h"

#include <QHash>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>

namespace ExtensionMenu {

bool run(QWidget *window, const Extensions::Command &command, const QJsonArray &selection)
{
    Extensions::Host *host = Extensions::Host::instance();
    const QString title = host->nameOf(command.extension);
    const QString verb = QString(command.label).remove(QStringLiteral("…"));
    if (!command.confirm.isEmpty()) {
        QMessageBox box(QMessageBox::Question, title, command.confirm, QMessageBox::NoButton, window);
        box.setTextFormat(Qt::PlainText);
        QPushButton *go = box.addButton(verb, QMessageBox::AcceptRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() != go)
            return false;
    }
    QString input;
    if (command.asks) {
        QInputDialog dialog(window);
        dialog.setWindowTitle(title);
        dialog.setLabelText(command.askLabel.isEmpty() ? verb : command.askLabel);
        dialog.setInputMode(QInputDialog::TextInput);
        dialog.setOption(QInputDialog::UsePlainTextEditForTextInput, command.askMultiline);
        dialog.setTextValue(command.askText);
        dialog.setOkButtonText(verb);
        dialog.resize(command.askMultiline ? QSize(460, 260) : QSize(420, dialog.sizeHint().height()));
        if (dialog.exec() != QDialog::Accepted)
            return false;
        input = dialog.textValue().trimmed();
        if (input.isEmpty())
            return false; // nothing typed is "never mind"
    }
    host->invoke(command, selection, input);
    return true;
}

namespace {

QAction *addCommand(QMenu *menu, QHash<QString, QMenu *> *groups, const Extensions::Command &command)
{
    QMenu *target = menu;
    QString path;
    const QStringList levels = command.group.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &level : levels) {
        path += (path.isEmpty() ? QString() : QStringLiteral("/")) + level;
        QMenu *&sub = (*groups)[path];
        if (!sub)
            sub = target->addMenu(QString(level).replace(QLatin1Char('&'), QStringLiteral("&&")));
        target = sub;
    }
    // A name is a name: an "&" in it isn't a shortcut key.
    QAction *action = target->addAction(QString(command.label).replace(QLatin1Char('&'), QStringLiteral("&&")));
    action->setProperty("argonautExtension", command.extension); // the command palette names it
    if (command.checked) {
        action->setCheckable(true);
        action->setChecked(true);
    }
    return action;
}

} // namespace

int add(QMenu *menu, QWidget *window, const QJsonArray &selection)
{
    const QList<Extensions::Command> commands = Extensions::Host::instance()->commands(int(selection.size()));
    QHash<QString, QMenu *> groups;
    for (const Extensions::Command &command : commands) {
        QAction *action = addCommand(menu, &groups, command);
        QObject::connect(action, &QAction::triggered, window, [window, command, selection] { run(window, command, selection); });
    }
    return int(commands.size());
}

void addToMenuBar(QMenu *menu, QWidget *window, const QString &which)
{
    static const char *const mark = "argonautExtensionCommand";
    const QList<QAction *> old = menu->actions();
    for (QAction *action : old) {
        if (action->property(mark).toBool()) {
            menu->removeAction(action);
            action->deleteLater();
        }
    }
    const QList<Extensions::Command> commands = Extensions::Host::instance()->menuCommands(which);
    if (commands.isEmpty())
        return;
    menu->addSeparator()->setProperty(mark, true);
    QHash<QString, QMenu *> groups;
    for (const Extensions::Command &command : commands) {
        const bool top = command.group.isEmpty();
        QAction *action = addCommand(menu, &groups, command);
        if (top)
            action->setProperty(mark, true);
        QObject::connect(action, &QAction::triggered, window, [window, command] { run(window, command, {}); });
    }
    for (QMenu *sub : std::as_const(groups)) {
        if (sub->parent() == menu)
            sub->menuAction()->setProperty(mark, true);
    }
}

} // namespace ExtensionMenu
