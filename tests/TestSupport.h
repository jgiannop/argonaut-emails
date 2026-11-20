#pragma once

#include <QTemporaryDir>
#include <QtGlobal>

// The first line of a test's main(), before the application object: a
// private HOME and XDG folders (settings, the mail store, password files
// never touch the real ones), no display, and never the real keyring.
namespace TestSupport {

class Sandbox
{
public:
    Sandbox()
    {
        const QByteArray root = m_dir.path().toUtf8();
        qputenv("HOME", root);
        qputenv("XDG_CONFIG_HOME", root + "/config");
        qputenv("XDG_DATA_HOME", root + "/data");
        qputenv("XDG_CACHE_HOME", root + "/cache");
        qputenv("XDG_RUNTIME_DIR", root + "/runtime");
        qputenv("MAIL_KEYSTORE", "file");
        qputenv("QT_QPA_PLATFORM", "offscreen");
        qputenv("ARGONAUT_THEMES_PATH", "");
    }
    QString path() const { return m_dir.path(); }

private:
    QTemporaryDir m_dir;
};

} // namespace TestSupport

#define MAIL_TEST_MAIN(TestClass, AppClass)                                                                            \
    int main(int argc, char **argv)                                                                                    \
    {                                                                                                                  \
        TestSupport::Sandbox sandbox;                                                                                  \
        AppClass app(argc, argv);                                                                                      \
        AppClass::setOrganizationName(QStringLiteral("ArgonautTests"));                                                \
        AppClass::setApplicationName(QStringLiteral("argonaut-emails-tests"));                                         \
        TestClass test;                                                                                                \
        return QTest::qExec(&test, argc, argv);                                                                        \
    }
