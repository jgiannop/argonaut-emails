#pragma once

#include "Extensions.h"

#include <QHash>
#include <QString>

class QWidget;

// The dialog for something an extension asks the user while it works: a
// login, an unknown host key, which shared folder. Text, fields and buttons
// are the extension's; the dialog is the program's, and always says which
// extension is asking (a password typed here goes to that extension).
namespace ExtensionForm {

struct Answer
{
    bool cancelled = true;
    QHash<QString, QString> values; // by field id; a tick box is "true" or "false"
    int button = 0;                 // which of the form's buttons
};

// Shows `form` over `window` and waits for the answer.
Answer ask(QWidget *window, const Extensions::Form &form);

} // namespace ExtensionForm
