#include "Theme.h"

#include <QGuiApplication>
#include <QStyleHints>
#include <QApplication>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QSettings>
#include <algorithm>
#include <QWidget>
#include <QTimer>
#include <QStandardPaths>
#include <QSet>
#include <QSaveFile>
#include <QRegularExpression>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFileSystemWatcher>
#include <QFileInfo>
#include <QFile>
#include <QEvent>
#include <QDir>

#include <initializer_list>

namespace {

constexpr auto ThemeKey = "appearance/theme";

// The platform's own palette, captured the first time we touch the
// palette (before we ever replace it).
const QPalette &systemPalette()
{
    static const QPalette palette = QApplication::palette();
    return palette;
}

ThemeMode s_mode = ThemeMode::System; // what applyThemeMode() last applied
QString s_fileKey;                    // a theme from a file is applied instead: its key…
Theme s_fileTheme;                    // …and its tokens

struct ThemeInfo
{
    ThemeMode mode;
    const char *key;  // saved in the settings
    const char *name; // shown in Preferences (translated)
};

const ThemeInfo kThemes[] = {
    {ThemeMode::System, "system", QT_TRANSLATE_NOOP("Theme", "Follow System")},
    {ThemeMode::Light, "light", QT_TRANSLATE_NOOP("Theme", "Light")},
    {ThemeMode::Dark, "dark", QT_TRANSLATE_NOOP("Theme", "Dark")},
    {ThemeMode::MidnightBlue, "midnight", QT_TRANSLATE_NOOP("Theme", "Midnight Blue")},
    {ThemeMode::RoseDawn, "rose", QT_TRANSLATE_NOOP("Theme", "Rosé Dawn")},
    {ThemeMode::Forest, "forest", QT_TRANSLATE_NOOP("Theme", "Forest")},
    {ThemeMode::Sunset, "sunset", QT_TRANSLATE_NOOP("Theme", "Sunset")},
};

const ThemeInfo &infoFor(ThemeMode mode)
{
    for (const ThemeInfo &info : kThemes) {
        if (info.mode == mode)
            return info;
    }
    return kThemes[0];
}

QColor withAlpha(QColor c, int alpha)
{
    c.setAlpha(alpha);
    return c;
}

// `over` (possibly translucent) painted on top of opaque `under`.
QColor blend(const QColor &over, const QColor &under)
{
    const qreal a = over.alphaF();
    return QColor::fromRgbF(over.redF() * a + under.redF() * (1 - a),
                            over.greenF() * a + under.greenF() * (1 - a),
                            over.blueF() * a + under.blueF() * (1 - a));
}

// The GNOME-like defaults (exact libadwaita colours).
Theme adwaita(bool dark, const QColor &accent)
{
    Theme t;
    t.dark = dark;
    t.accent = accent;
    if (dark) {
        t.window = QColor("#242424");
        t.sidebar = QColor("#303030");
        t.outline = QColor("#3d3d3d");
        t.fg = QColor(255, 255, 255);
        t.dimFg = QColor(255, 255, 255, 140);
        t.hover = QColor(255, 255, 255, 18);
        t.pressed = QColor(255, 255, 255, 36);
        t.selected = QColor(255, 255, 255, 26);
        t.pathBg = QColor(255, 255, 255, 20);
        t.button = QColor(255, 255, 255, 26);
        t.buttonHover = QColor(255, 255, 255, 46);
        t.popup = QColor("#383838");
        t.border = QColor(0, 0, 0, 90);
        t.scroll = QColor(255, 255, 255, 60);
        t.scrollHover = QColor(255, 255, 255, 110);
    } else {
        t.window = QColor("#fafafa");
        t.sidebar = QColor("#ebebeb");
        t.outline = QColor("#c8c8c8");
        t.fg = QColor(0, 0, 0, 204);
        t.dimFg = QColor(0, 0, 0, 140);
        t.hover = QColor(0, 0, 0, 13);
        t.pressed = QColor(0, 0, 0, 30);
        t.selected = QColor(0, 0, 0, 26);
        t.pathBg = QColor(0, 0, 0, 15);
        t.button = QColor(0, 0, 0, 20);
        t.buttonHover = QColor(0, 0, 0, 36);
        t.popup = QColor("#ffffff");
        t.border = QColor(0, 0, 0, 30);
        t.scroll = QColor(0, 0, 0, 60);
        t.scrollHover = QColor(0, 0, 0, 110);
    }
    t.itemSelected = withAlpha(t.accent, dark ? 80 : 60);
    return t;
}

// A coloured theme from four base colours; every other token is derived
// the same way as in the defaults, so all themes behave alike.
Theme custom(bool dark, const QColor &window, const QColor &sidebar, const QColor &fg,
             const QColor &accent)
{
    Theme t;
    t.dark = dark;
    t.window = window;
    t.sidebar = sidebar;
    t.fg = fg;
    t.accent = accent;
    if (dark) {
        t.outline = sidebar.lighter(135);
        t.dimFg = withAlpha(fg, 150);
        t.hover = withAlpha(fg, 20);
        t.pressed = withAlpha(fg, 38);
        t.selected = withAlpha(fg, 28);
        t.pathBg = withAlpha(fg, 22);
        t.button = withAlpha(fg, 28);
        t.buttonHover = withAlpha(fg, 48);
        t.popup = sidebar.lighter(118);
        t.border = QColor(0, 0, 0, 100);
        t.scroll = withAlpha(fg, 64);
        t.scrollHover = withAlpha(fg, 115);
        t.itemSelected = withAlpha(accent, 85);
    } else {
        t.outline = sidebar.darker(118);
        t.dimFg = withAlpha(fg, 165);
        t.hover = withAlpha(fg, 16);
        t.pressed = withAlpha(fg, 34);
        t.selected = withAlpha(fg, 30);
        t.pathBg = withAlpha(fg, 18);
        t.button = withAlpha(fg, 22);
        t.buttonHover = withAlpha(fg, 40);
        t.popup = window.lighter(103);
        t.border = withAlpha(fg, 40);
        t.scroll = withAlpha(fg, 64);
        t.scrollHover = withAlpha(fg, 115);
        t.itemSelected = withAlpha(accent, 64);
    }
    return t;
}

// Qt palette (for widgets our style sheet doesn't cover: text fields,
// dialogs, tooltips…) matching a theme.
QPalette paletteFor(const Theme &t)
{
    const QColor text = blend(t.fg, t.window);
    const QColor base = t.dark ? t.window.darker(112) : t.window.lighter(104);
    const QColor disabled = blend(withAlpha(t.fg, 110), t.window);

    QPalette p;
    p.setColor(QPalette::Window, t.window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, blend(t.hover, base));
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, t.sidebar);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, t.dark ? QColor("#ff7b63") : QColor("#c01c28"));
    p.setColor(QPalette::ToolTipBase, t.popup);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::PlaceholderText, blend(t.dimFg, base));
    p.setColor(QPalette::Highlight, t.accent);
    p.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    p.setColor(QPalette::Link, t.accent);
    // Fusion's bevel shades
    p.setColor(QPalette::Light, t.sidebar.lighter(130));
    p.setColor(QPalette::Midlight, t.sidebar.lighter(115));
    p.setColor(QPalette::Mid, t.sidebar.darker(130));
    p.setColor(QPalette::Dark, t.sidebar.darker(160));
    p.setColor(QPalette::Shadow, QColor(0, 0, 0, t.dark ? 160 : 90));
    for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, disabled);
    return p;
}

} // namespace

QList<ThemeMode> allThemeModes()
{
    QList<ThemeMode> modes;
    for (const ThemeInfo &info : kThemes)
        modes.append(info.mode);
    return modes;
}

QString themeModeName(ThemeMode mode)
{
    return QCoreApplication::translate("Theme", infoFor(mode).name);
}

ThemeMode savedThemeMode()
{
    const QString value = QSettings().value(ThemeKey, "system").toString();
    for (const ThemeInfo &info : kThemes) {
        if (value == QLatin1String(info.key))
            return info.mode;
    }
    return ThemeMode::System;
}

// On Windows the frame of a window (a dialog's title bar) is the system's,
// light or dark by what the system is told the program is: said here, or a
// dark theme's dialogs get a white title bar. (Qt 6.8 has the call; before
// it the frame follows the system's own setting.)
static void tellSystemTheScheme(bool follow, bool dark)
{
#if defined(Q_OS_WIN) && QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    if (follow)
        QGuiApplication::styleHints()->unsetColorScheme();
    else
        QGuiApplication::styleHints()->setColorScheme(dark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
#else
    Q_UNUSED(follow);
    Q_UNUSED(dark);
#endif
}

void applyThemeMode(ThemeMode mode)
{
    const QPalette &system = systemPalette();
    s_fileKey.clear();
    s_mode = mode;
    tellSystemTheScheme(mode == ThemeMode::System, mode == ThemeMode::System ? false : Theme::forMode(mode).dark);
    if (mode == ThemeMode::System) {
        // Leave the palette untouched at startup so Qt keeps following the
        // platform; only restore it if we replaced it earlier.
        if (QApplication::palette() != system)
            QApplication::setPalette(system);
        return;
    }
    QApplication::setPalette(paletteFor(Theme::forMode(mode)));
}

void setThemeMode(ThemeMode mode)
{
    QSettings().setValue(ThemeKey, QString::fromLatin1(infoFor(mode).key));
    applyThemeMode(mode);
}

Theme currentTheme()
{
    if (!s_fileKey.isEmpty())
        return s_fileTheme;
    return s_mode == ThemeMode::System ? Theme::fromPalette(QApplication::palette())
                                       : Theme::forMode(s_mode);
}

// ------------------------------------------------- themes from files ---

namespace {

// Where theme files are looked for, the user's own first. ARGONAUT_THEMES_PATH
// replaces them all (a colon list; tests).
QStringList themeFolders()
{
    if (qEnvironmentVariableIsSet("ARGONAUT_THEMES_PATH"))
        return qEnvironmentVariable("ARGONAUT_THEMES_PATH").split(QLatin1Char(':'), Qt::SkipEmptyParts);
    QStringList folders{userThemesFolder(), QCoreApplication::applicationDirPath() + QStringLiteral("/themes")};
    const QStringList system = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
    for (const QString &dir : system) {
        const QString folder = dir + QStringLiteral("/argonaut/themes");
        if (!folders.contains(folder))
            folders << folder;
    }
    return folders;
}

// "file:harbor" → its file, the first found; empty if there is none.
QString fileOfKey(const QString &key)
{
    static const QRegularExpression good(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9 ._-]{0,63}$"));
    const QString name = key.mid(5);
    if (!key.startsWith(QLatin1String("file:")) || !good.match(name).hasMatch())
        return {};
    const QStringList folders = themeFolders();
    for (const QString &folder : folders) {
        const QString path = folder + QLatin1Char('/') + name + QStringLiteral(".json");
        if (QFileInfo(path).isFile())
            return path;
    }
    return {};
}

// The folders are watched, so a theme being written shows as it is saved.
class ThemeWatcher : public QObject
{
public:
    static void start()
    {
        static ThemeWatcher *watcher = nullptr;
        if (!watcher && qApp)
            watcher = new ThemeWatcher(qApp);
        if (watcher)
            watcher->watchFolders();
    }

private:
    explicit ThemeWatcher(QObject *parent) : QObject(parent)
    {
        m_later.setSingleShot(true);
        m_later.setInterval(150);
        connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_later, qOverload<>(&QTimer::start));
        connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_later, qOverload<>(&QTimer::start));
        connect(&m_later, &QTimer::timeout, this, [this] {
            watchFolders();
            if (!s_fileKey.isEmpty())
                applyThemeKey(s_fileKey); // the file being edited: its new colours now
        });
    }
    void watchFolders()
    {
        const QStringList folders = themeFolders();
        for (const QString &folder : folders) {
            if (QFileInfo(folder).isDir() && !m_watcher.directories().contains(folder))
                m_watcher.addPath(folder);
        }
        // The file itself too: an editor that writes in place changes no folder.
        const QString file = fileOfKey(s_fileKey);
        if (!file.isEmpty() && !m_watcher.files().contains(file))
            m_watcher.addPath(file);
    }
    QFileSystemWatcher m_watcher;
    QTimer m_later;
};

} // namespace

QString userThemesFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/argonaut/themes");
}

ThemeFile readThemeFile(const QString &path)
{
    ThemeFile out;
    out.name = QFileInfo(path).completeBaseName();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        out.problem = QCoreApplication::translate("Theme", "It can't be read.");
        return out;
    }
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.read(64 * 1024), &error);
    if (!doc.isObject()) {
        out.problem = QCoreApplication::translate("Theme", "It isn't a theme file: %1").arg(error.errorString());
        return out;
    }
    const QJsonObject o = doc.object();
    // A colour: "#rrggbb", "#aarrggbb", or a name like "white".
    auto colour = [&o](const char *key) {
        const QString text = o.value(QLatin1String(key)).toString().trimmed();
        return text.isEmpty() || text.size() > 40 ? QColor() : QColor(text);
    };
    const QColor window = colour("window"), text = colour("text"), accent = colour("accent");
    if (!window.isValid() || !text.isValid() || !accent.isValid()) {
        out.problem = QCoreApplication::translate("Theme", "It needs the colours \"window\", \"text\" and \"accent\" (like \"#1e2a36\").");
        return out;
    }
    const QString name = o.value(QLatin1String("name")).toString().simplified().left(40);
    if (!name.isEmpty())
        out.name = name;
    // Dark or light: said, or told from the background.
    const bool dark = o.contains(QLatin1String("dark")) ? o.value(QLatin1String("dark")).toBool() : window.lightness() < 128;
    QColor sidebar = colour("sidebar");
    if (!sidebar.isValid())
        sidebar = dark ? window.lighter(118) : window.darker(104);
    Theme t = custom(dark, QColor(window.rgb()), QColor(sidebar.rgb()), QColor(text.rgb()), QColor(accent.rgb()));
    // Every other colour is worked out from those four; any of them can be
    // said outright instead.
    const struct { const char *key; QColor Theme::*token; } others[] = {
        {"dim", &Theme::dimFg},           {"outline", &Theme::outline},       {"hover", &Theme::hover},
        {"pressed", &Theme::pressed},     {"sidebarSelection", &Theme::selected}, {"selection", &Theme::itemSelected},
        {"pathBar", &Theme::pathBg},      {"button", &Theme::button},         {"buttonHover", &Theme::buttonHover},
        {"popup", &Theme::popup},         {"border", &Theme::border},         {"scrollbar", &Theme::scroll},
        {"scrollbarHover", &Theme::scrollHover},
    };
    for (const auto &other : others) {
        const QColor value = colour(other.key);
        if (value.isValid())
            t.*(other.token) = value;
    }
    // A new id whenever the file's colours change: that is what makes the
    // window dress again.
    file.seek(0);
    t.id = QStringLiteral("file:%1:%2").arg(QFileInfo(path).completeBaseName()).arg(qHash(file.read(64 * 1024)));
    out.theme = t;
    return out;
}

QString saveThemeFile(const Theme &theme, const QString &name, QString *error)
{
    const QString folder = userThemesFolder();
    QString base = name.simplified();
    base.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9 ._-]")), QStringLiteral("-"));
    base = base.left(40).trimmed();
    if (base.isEmpty() || base.startsWith(QLatin1Char('.')))
        base = QStringLiteral("My Theme");
    if (!QDir().mkpath(folder)) {
        if (error)
            *error = QCoreApplication::translate("Theme", "The themes folder couldn't be made.");
        return {};
    }
    QString path = folder + QLatin1Char('/') + base + QStringLiteral(".json");
    for (int n = 2; QFileInfo::exists(path); ++n)
        path = folder + QLatin1Char('/') + base + QStringLiteral(" %1.json").arg(n);
    // The four colours everything comes from, then the rest spelled out,
    // so each can be changed by itself. Translucent ones keep their alpha.
    auto hex = [](const QColor &c) { return c.alpha() == 255 ? c.name(QColor::HexRgb) : c.name(QColor::HexArgb); };
    QByteArray text = "{\n";
    auto line = [&text, &hex](const char *key, const QColor &c, bool last = false) {
        text += "    \"" + QByteArray(key) + "\": \"" + hex(c).toLatin1() + (last ? "\"\n" : "\",\n");
    };
    text += "    \"name\": " + QJsonDocument(QJsonArray{QFileInfo(path).completeBaseName()}).toJson(QJsonDocument::Compact).mid(1).chopped(1) + ",\n";
    text += QByteArray("    \"dark\": ") + (theme.dark ? "true" : "false") + ",\n";
    line("window", theme.window);
    line("sidebar", theme.sidebar);
    line("text", theme.fg);
    line("accent", theme.accent);
    line("dim", theme.dimFg);
    line("selection", theme.itemSelected);
    line("sidebarSelection", theme.selected);
    line("hover", theme.hover);
    line("pressed", theme.pressed);
    line("pathBar", theme.pathBg);
    line("button", theme.button);
    line("buttonHover", theme.buttonHover);
    line("popup", theme.popup);
    line("border", theme.border);
    line("outline", theme.outline);
    line("scrollbar", theme.scroll);
    line("scrollbarHover", theme.scrollHover, true);
    text += "}\n";
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(text) != text.size() || !file.commit()) {
        if (error)
            *error = QCoreApplication::translate("Theme", "The file couldn't be written.");
        return {};
    }
    return path;
}

QList<ThemeChoice> allThemes()
{
    QList<ThemeChoice> out;
    for (const ThemeInfo &info : kThemes)
        out.append({QString::fromLatin1(info.key), QCoreApplication::translate("Theme", info.name), QString()});
    // The files, by name; the first folder that has a name wins (the user's
    // own copy over one that came with Argonaut). Broken ones aren't offered.
    QList<ThemeChoice> files;
    QSet<QString> seen;
    const QStringList folders = themeFolders();
    for (const QString &folder : folders) {
        const QStringList names = QDir(folder).entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
        for (const QString &name : names) {
            const QString base = QFileInfo(name).completeBaseName();
            const QString key = QStringLiteral("file:") + base;
            if (files.size() >= 200 || seen.contains(base) || fileOfKey(key).isEmpty())
                continue;
            seen.insert(base);
            const ThemeFile read = readThemeFile(folder + QLatin1Char('/') + name);
            if (read.problem.isEmpty())
                files.append({key, read.name, folder + QLatin1Char('/') + name});
        }
    }
    std::sort(files.begin(), files.end(), [](const ThemeChoice &a, const ThemeChoice &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    return out + files;
}

QString savedThemeKey()
{
    return QSettings().value(ThemeKey, "system").toString();
}

QString currentThemeKey()
{
    return s_fileKey.isEmpty() ? QString::fromLatin1(infoFor(s_mode).key) : s_fileKey;
}

Theme themeForKey(const QString &key)
{
    for (const ThemeInfo &info : kThemes) {
        if (key == QLatin1String(info.key))
            return info.mode == ThemeMode::System ? Theme::fromPalette(systemPalette()) : Theme::forMode(info.mode);
    }
    const QString file = fileOfKey(key);
    if (!file.isEmpty()) {
        const ThemeFile read = readThemeFile(file);
        if (read.problem.isEmpty())
            return read.theme;
    }
    return Theme::fromPalette(systemPalette());
}

void applyThemeKey(const QString &key)
{
    ThemeWatcher::start();
    for (const ThemeInfo &info : kThemes) {
        if (key == QLatin1String(info.key)) {
            applyThemeMode(info.mode);
            return;
        }
    }
    const QString file = fileOfKey(key);
    const ThemeFile read = file.isEmpty() ? ThemeFile() : readThemeFile(file);
    if (file.isEmpty() || !read.problem.isEmpty()) {
        // Gone, or broken while being edited: the last good colours stay
        // if it is the one applied; otherwise the system's.
        if (key != s_fileKey || file.isEmpty())
            applyThemeMode(ThemeMode::System);
        return;
    }
    systemPalette(); // captured before it is replaced
    const bool same = key == s_fileKey && read.theme.id == s_fileTheme.id;
    s_fileKey = key;
    s_fileTheme = read.theme;
    ThemeWatcher::start(); // its file is watched now
    if (same)
        return;
    tellSystemTheScheme(false, read.theme.dark);
    const QPalette palette = paletteFor(read.theme);
    if (QApplication::palette() != palette) {
        QApplication::setPalette(palette); // every window dresses again
    } else {
        // Only colours the palette doesn't hold changed: windows are told the same way.
        const QWidgetList windows = QApplication::topLevelWidgets();
        for (QWidget *window : windows) {
            QEvent event(QEvent::ApplicationPaletteChange);
            QApplication::sendEvent(window, &event);
        }
    }
}

void setThemeKey(const QString &key)
{
    QSettings().setValue(ThemeKey, key);
    applyThemeKey(key);
}

Theme Theme::fromPalette(const QPalette &palette)
{
    const bool dark = palette.color(QPalette::Window).lightness() < 128;
    Theme t = adwaita(dark, palette.color(QPalette::Highlight));
    t.id = dark ? QStringLiteral("system-dark") : QStringLiteral("system-light");
    return t;
}

Theme Theme::forMode(ThemeMode mode)
{
    const QColor systemAccent = systemPalette().color(QPalette::Highlight);
    Theme t;
    switch (mode) {
    case ThemeMode::System:
        return fromPalette(systemPalette());
    case ThemeMode::Light:
        t = adwaita(false, systemAccent);
        break;
    case ThemeMode::Dark:
        t = adwaita(true, systemAccent);
        break;
    case ThemeMode::MidnightBlue: // deep navy, sky-blue accent
        t = custom(true, QColor("#141b26"), QColor("#1b2433"), QColor("#e6edf7"),
                   QColor("#5aa9ff"));
        break;
    case ThemeMode::RoseDawn: // warm cream, blush sidebar, rose accent
        t = custom(false, QColor("#fbf6f3"), QColor("#f3e6e4"), QColor("#4a3536"),
                   QColor("#d46a7e"));
        break;
    case ThemeMode::Forest: // deep green-grey, mint accent
        t = custom(true, QColor("#18201b"), QColor("#1f2a24"), QColor("#e3ece5"),
                   QColor("#5fcf8f"));
        break;
    case ThemeMode::Sunset: // plum and charcoal, amber accent
        t = custom(true, QColor("#201826"), QColor("#2a2030"), QColor("#f2e8f0"),
                   QColor("#ff9e5e"));
        break;
    }
    t.id = QString::fromLatin1(infoFor(mode).key);
    return t;
}

QString cssColor(const QColor &c)
{
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(c.red())
        .arg(c.green())
        .arg(c.blue())
        .arg(c.alphaF(), 0, 'f', 3);
}

namespace {

// All glyphs are designed on a 16x16 grid with a 1.5px stroke.
void paintGlyph(QPainter *p, const QRectF &r, Glyph g, const QColor &c)
{
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    const qreal s = qMin(r.width(), r.height()) / 16.0;
    p->translate(r.center().x() - 8 * s, r.center().y() - 8 * s);
    p->scale(s, s);

    const QPen pen(c, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p->setPen(pen);
    p->setBrush(Qt::NoBrush);

    auto line = [&](qreal x1, qreal y1, qreal x2, qreal y2) {
        p->drawLine(QPointF(x1, y1), QPointF(x2, y2));
    };
    auto poly = [&](std::initializer_list<QPointF> pts, bool closed = false) {
        QPainterPath path;
        bool first = true;
        for (const QPointF &pt : pts) {
            if (first)
                path.moveTo(pt);
            else
                path.lineTo(pt);
            first = false;
        }
        if (closed)
            path.closeSubpath();
        p->drawPath(path);
    };
    auto filled = [&](auto draw) {
        p->setPen(Qt::NoPen);
        p->setBrush(c);
        draw();
        p->setBrush(Qt::NoBrush);
        p->setPen(pen);
    };

    switch (g) {
    case Glyph::Home:
        poly({{2, 8.25}, {8, 2.5}, {14, 8.25}});
        poly({{3.75, 6.75}, {3.75, 13.5}, {12.25, 13.5}, {12.25, 6.75}});
        poly({{6.75, 13.5}, {6.75, 10}, {9.25, 10}, {9.25, 13.5}});
        break;
    case Glyph::Desktop:
        p->drawRoundedRect(QRectF(1.75, 2.5, 12.5, 8.5), 1.5, 1.5);
        line(8, 11, 8, 13.5);
        line(5, 13.5, 11, 13.5);
        break;
    case Glyph::Documents:
        poly({{3.5, 1.75}, {9.25, 1.75}, {12.5, 5}, {12.5, 14.25}, {3.5, 14.25}}, true);
        poly({{9.25, 1.75}, {9.25, 5}, {12.5, 5}});
        line(5.75, 8, 10.25, 8);
        line(5.75, 10.75, 10.25, 10.75);
        break;
    case Glyph::Downloads:
        line(8, 2, 8, 10);
        poly({{4.5, 6.75}, {8, 10.25}, {11.5, 6.75}});
        poly({{2.5, 10.75}, {2.5, 13.5}, {13.5, 13.5}, {13.5, 10.75}});
        break;
    case Glyph::Music:
        poly({{6, 12}, {6, 3.25}, {13, 1.75}, {13, 10.5}});
        filled([&] {
            p->drawEllipse(QPointF(4.25, 12), 2.1, 1.7);
            p->drawEllipse(QPointF(11.25, 10.5), 2.1, 1.7);
        });
        break;
    case Glyph::Pictures:
        p->drawRoundedRect(QRectF(1.75, 2.75, 12.5, 10.5), 1.5, 1.5);
        poly({{2.25, 12}, {6, 8.25}, {9, 11.25}, {11, 9.25}, {13.75, 12}});
        filled([&] { p->drawEllipse(QPointF(10.75, 6), 1.3, 1.3); });
        break;
    case Glyph::Videos:
        p->drawRoundedRect(QRectF(1.75, 3, 12.5, 10), 1.5, 1.5);
        filled([&] {
            QPainterPath tri;
            tri.moveTo(6.5, 5.75);
            tri.lineTo(10.75, 8);
            tri.lineTo(6.5, 10.25);
            tri.closeSubpath();
            p->drawPath(tri);
        });
        break;
    case Glyph::Trash:
        line(2.5, 4, 13.5, 4);
        poly({{6, 4}, {6, 2.25}, {10, 2.25}, {10, 4}});
        poly({{3.75, 4}, {4.5, 14}, {11.5, 14}, {12.25, 4}});
        line(6.75, 6.75, 6.75, 11.25);
        line(9.25, 6.75, 9.25, 11.25);
        break;
    case Glyph::Drive:
        p->drawRoundedRect(QRectF(1.75, 4.5, 12.5, 7), 1.5, 1.5);
        line(4.25, 8, 7.75, 8);
        filled([&] { p->drawEllipse(QPointF(11.25, 8), 0.95, 0.95); });
        break;
    case Glyph::Network: // a server: two stacked boxes with lights
        p->drawRoundedRect(QRectF(2.25, 2.25, 11.5, 5), 1.25, 1.25);
        p->drawRoundedRect(QRectF(2.25, 8.75, 11.5, 5), 1.25, 1.25);
        filled([&] {
            p->drawEllipse(QPointF(11, 4.75), 0.85, 0.85);
            p->drawEllipse(QPointF(11, 11.25), 0.85, 0.85);
        });
        break;
    case Glyph::Cloud: { // storage somewhere else (an extension's place)
        QPainterPath cloud;
        cloud.moveTo(4.75, 12.5);
        cloud.cubicTo(1.5, 12.5, 1.5, 8.0, 4.5, 7.75);
        cloud.cubicTo(4.75, 3.75, 10.25, 3.25, 11.0, 6.75);
        cloud.cubicTo(15.0, 6.75, 15.0, 12.5, 11.5, 12.5);
        cloud.closeSubpath();
        p->drawPath(cloud);
        break;
    }
    case Glyph::Removable:
        p->drawRoundedRect(QRectF(4.25, 5.5, 7.5, 8.75), 1.5, 1.5);
        p->drawRect(QRectF(6, 1.75, 4, 3.75));
        break;
    case Glyph::Search:
        p->drawEllipse(QPointF(7, 7), 4.25, 4.25);
        line(10.25, 10.25, 13.5, 13.5);
        break;
    case Glyph::Menu:
        for (qreal y : {4.0, 8.0, 12.0})
            line(2.75, y, 13.25, y);
        break;
    case Glyph::Kebab:
        filled([&] {
            for (qreal y : {3.5, 8.0, 12.5})
                p->drawEllipse(QPointF(8, y), 1.4, 1.4);
        });
        break;
    case Glyph::ChevronLeft:
        poly({{10, 3.5}, {5.5, 8}, {10, 12.5}});
        break;
    case Glyph::ChevronRight:
        poly({{6, 3.5}, {10.5, 8}, {6, 12.5}});
        break;
    case Glyph::ChevronUp:
        poly({{3.5, 10}, {8, 5.5}, {12.5, 10}});
        break;
    case Glyph::ChevronDown:
        poly({{3.5, 6}, {8, 10.5}, {12.5, 6}});
        break;
    case Glyph::Grid:
        for (const QRectF &rr : {QRectF(2.5, 2.5, 4.5, 4.5), QRectF(9, 2.5, 4.5, 4.5),
                                 QRectF(2.5, 9, 4.5, 4.5), QRectF(9, 9, 4.5, 4.5)})
            p->drawRoundedRect(rr, 1, 1);
        break;
    case Glyph::List:
        for (qreal y : {4.0, 8.0, 12.0})
            line(6.5, y, 13.5, y);
        filled([&] {
            for (qreal y : {4.0, 8.0, 12.0})
                p->drawEllipse(QPointF(3, y), 1.1, 1.1);
        });
        break;
    case Glyph::Minimize:
        line(4.5, 11, 11.5, 11);
        break;
    case Glyph::Maximize:
        p->drawRoundedRect(QRectF(4.25, 4.25, 7.5, 7.5), 1.2, 1.2);
        break;
    case Glyph::Restore:
        p->drawRoundedRect(QRectF(3.75, 6.25, 6, 6), 1, 1);
        poly({{6.5, 3.75}, {12.25, 3.75}, {12.25, 9.5}});
        break;
    case Glyph::Close:
        line(4.75, 4.75, 11.25, 11.25);
        line(11.25, 4.75, 4.75, 11.25);
        break;
    case Glyph::Folder:
        poly({{1.75, 6}, {1.75, 3.75}, {6, 3.75}, {7.5, 5.25}, {14.25, 5.25}, {14.25, 12.75},
              {1.75, 12.75}, {1.75, 6}});
        line(1.75, 7.25, 14.25, 7.25);
        break;
    case Glyph::Split: // two panes side by side
        p->drawRoundedRect(QRectF(2.25, 3.25, 11.5, 9.5), 1.5, 1.5);
        line(8, 3.5, 8, 12.5);
        break;
    case Glyph::Branch: { // a git branch: two commits on a line, one off to the side
        p->drawEllipse(QPointF(5, 3.75), 1.75, 1.75);
        p->drawEllipse(QPointF(5, 12.25), 1.75, 1.75);
        p->drawEllipse(QPointF(11, 5.25), 1.75, 1.75);
        line(5, 5.5, 5, 10.5);
        QPainterPath fork;
        fork.moveTo(11, 7);
        fork.quadTo(11, 10, 5.5, 10.25);
        p->drawPath(fork);
        break;
    }
    case Glyph::Recent: // a clock
        p->drawEllipse(QPointF(8, 8), 5.75, 5.75);
        poly({{8, 4.75}, {8, 8.25}, {10.5, 9.75}});
        break;
    case Glyph::Star:
        poly({{8, 2.25}, {9.7, 6}, {13.75, 6.35}, {10.65, 9}, {11.6, 13}, {8, 10.85},
              {4.4, 13}, {5.35, 9}, {2.25, 6.35}, {6.3, 6}}, true);
        break;
    case Glyph::Usage: // a pie chart: disk usage
        p->drawEllipse(QPointF(8, 8), 5.75, 5.75);
        filled([&] {
            QPainterPath slice;
            slice.moveTo(8, 8);
            slice.arcTo(QRectF(2.25, 2.25, 11.5, 11.5), 90, -125);
            slice.closeSubpath();
            p->drawPath(slice);
        });
        break;
    case Glyph::Eject:
        poly({{3, 9.5}, {8, 3.75}, {13, 9.5}}, true);
        line(3, 12.5, 13, 12.5);
        break;
    case Glyph::Terminal:
        p->drawRoundedRect(QRectF(1.75, 2.5, 12.5, 11), 1.5, 1.5);
        poly({{4.5, 6}, {6.75, 8}, {4.5, 10}});
        line(8.25, 10.25, 11.25, 10.25);
        break;
    }
    p->restore();
}

class GlyphEngine : public QIconEngine
{
public:
    GlyphEngine(Glyph glyph, const QColor &color) : m_glyph(glyph), m_color(color) {}

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State) override
    {
        QColor c = m_color;
        if (mode == QIcon::Disabled)
            c.setAlphaF(c.alphaF() * 0.35);
        paintGlyph(painter, rect, m_glyph, c);
    }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        QPixmap pm(size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        paint(&p, QRect(QPoint(0, 0), size), mode, state);
        return pm;
    }

    QIconEngine *clone() const override { return new GlyphEngine(m_glyph, m_color); }

private:
    Glyph m_glyph;
    QColor m_color;
};

} // namespace

QIcon glyphIcon(Glyph glyph, const QColor &color)
{
    return QIcon(new GlyphEngine(glyph, color));
}

// The app icon: Argonaut's orange with its boat, on a letter instead of a
// folder. Same geometry as data/argonaut-emails.svg (128x128), drawn in code
// so no SVG plugin is needed.
QIcon applicationIcon()
{
    QIcon icon;
    for (int size : {16, 24, 32, 48, 64, 128, 256}) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(size / 128.0, size / 128.0);
        p.setPen(Qt::NoPen);
        // The envelope: its back edge, its front and the flap.
        p.setBrush(QColor("#c64600"));
        p.drawRoundedRect(QRectF(10, 24, 108, 84), 10, 10);
        QLinearGradient front(0, 28, 0, 108);
        front.setColorAt(0, QColor("#ffa348"));
        front.setColorAt(1, QColor("#ff7800"));
        p.setBrush(front);
        p.drawRoundedRect(QRectF(10, 28, 108, 80), 10, 10);
        p.setPen(QPen(QColor("#e66100"), 6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(QPolygonF({QPointF(16, 34), QPointF(64, 56), QPointF(112, 34)}));

        // The Argo (Argonaut's mast, mainsail, jib and hull), a little smaller, under the flap.
        p.translate(64, 65);
        p.scale(0.76, 0.76);
        p.translate(-66, -54);
        p.setPen(QPen(Qt::white, 5, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(66, 54), QPointF(66, 90));
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        p.drawPolygon(QPolygonF({QPointF(70, 55), QPointF(92, 86), QPointF(70, 86)}));
        p.drawPolygon(QPolygonF({QPointF(34, 92), QPointF(98, 92), QPointF(89, 104), QPointF(43, 104)}));
        p.setBrush(QColor(255, 255, 255, 217)); // 85 %
        p.drawPolygon(QPolygonF({QPointF(62, 61), QPointF(62, 86), QPointF(44, 86)}));
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}
