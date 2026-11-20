// What the window does for extensions (a part of MainWindow in a file of its
// own, as in Argonaut): their commands in the menus, their results in the
// message pane, their forms over the window, and the Claude settings.

#include "MainWindow.h"

#include "Account.h"
#include "ClaudeDialog.h"
#include "ClaudeReports.h"
#include "ExtensionForm.h"
#include "ExtensionMenu.h"
#include "ExtensionsDialog.h"
#include "MessageList.h"
#include "MessageView.h"
#include "SafeHtml.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QStatusBar>
#include <QTextDocument>
#include <QUrl>

void MainWindow::connectExtensions()
{
    Extensions::Host *host = Extensions::Host::instance();
    host->setMail(m_store, [this](const QString &account) { return m_syncs.value(account); });
    connect(host, &Extensions::Host::message, this, [this](const QString &extension, const QString &text) {
        statusBar()->showMessage(QStringLiteral("%1: %2").arg(extension, text), 10000);
    });
    connect(host, &Extensions::Host::showRequested, this, &MainWindow::showExtensionResult);
    // Queued: a form is a dialog, and the extension's message is still being read.
    connect(host, &Extensions::Host::formRequested, this, &MainWindow::showExtensionForm, Qt::QueuedConnection);
    connect(host, &Extensions::Host::claudeWaiting, this, &MainWindow::showClaudeWaiting);
    host->start();
}

void MainWindow::refreshExtensionMenus()
{
    for (auto it = m_extensionMenus.constBegin(); it != m_extensionMenus.constEnd(); ++it)
        ExtensionMenu::addToMenuBar(it.value(), this, it.key());
}

// The chosen message, as an extension is told about it.
QJsonArray MainWindow::extensionSelection() const
{
    const MailStore::Summary s = m_list->current();
    if (s.uid == 0)
        return {};
    const MailStore::Folder folder = m_store->folder(s.account, s.folder);
    return QJsonArray{QJsonObject{{QStringLiteral("account"), Account::byId(s.account).email},
                                  {QStringLiteral("folder"), folder.name},
                                  {QStringLiteral("role"), folder.role},
                                  {QStringLiteral("uid"), double(s.uid)},
                                  {QStringLiteral("from"), s.fromName},
                                  {QStringLiteral("fromEmail"), s.fromEmail},
                                  {QStringLiteral("subject"), s.subject},
                                  {QStringLiteral("date"), s.date.toString(Qt::ISODate)}}};
}

// A result in the message pane: Markdown turned into HTML here, made safe
// as a message is, and shown locked as a message is.
void MainWindow::showExtensionResult(const Extensions::Result &result)
{
    QString html;
    if (result.kind == Extensions::Result::Markdown) {
        QTextDocument document;
        document.setMarkdown(result.text);
        html = SafeHtml::clean(document.toHtml()).html;
    } else {
        html = SafeHtml::fromPlainText(result.text);
    }
    const QString name = Extensions::Host::instance()->nameOf(result.extension);
    m_shown = {};
    m_view->showResult(result.title.isEmpty() ? name : result.title, html,
                       result.note.isEmpty() ? tr("From the extension “%1”").arg(name) : result.note);
    updateActions();
}

void MainWindow::showExtensionForm(const Extensions::Form &form)
{
    Extensions::Host *host = Extensions::Host::instance();
    if (!host->takeForm(form.extension, form.id))
        return; // another window has it
    const ExtensionForm::Answer answer = ExtensionForm::ask(this, form);
    host->answerForm(form.extension, form.id, answer.cancelled, answer.values, answer.button);
}

// Claude is being asked something for an extension, which takes a while.
void MainWindow::showClaudeWaiting(const QString &, const QString &name, int waiting)
{
    if (waiting > 0) {
        m_shown = {};
        m_view->showNote(tr("Waiting for Claude's answer (%1)…").arg(name));
        m_status->setText(tr("Waiting for Claude…"));
    } else {
        m_status->clear();
    }
}

void MainWindow::showExtensions()
{
    ExtensionsDialog dialog(this);
    connect(&dialog, &ExtensionsDialog::openFolderRequested, this, [](const QString &folder) {
        QDir().mkpath(folder);
        QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
    });
    connect(&dialog, &ExtensionsDialog::guideRequested, this, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/jgiannop/argonaut-emails/blob/master/docs/extensions.md")));
    });
    dialog.exec();
}

void MainWindow::showClaudeSettings()
{
    ClaudeDialog dialog(this);
    connect(&dialog, &ClaudeDialog::openReportsRequested, this, &MainWindow::openClaudeReports);
    dialog.exec();
}

// File → Claude Reports: the folder where every answer is kept, in the file manager.
void MainWindow::openClaudeReports()
{
    const QString folder = ClaudeReports::folder();
    QDir().mkpath(folder);
    QFile::setPermissions(folder, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}
