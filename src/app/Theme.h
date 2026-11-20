#pragma once

#include <QColor>
#include <QIcon>
#include <QList>
#include <QPalette>
#include <QString>

// Colour tokens modelled on GNOME's libadwaita. The defaults (Light/Dark,
// or the platform's choice for "Follow System") use libadwaita's colours;
// the named themes are derived from four base colours each.
enum class ThemeMode { System, Light, Dark, MidnightBlue, RoseDawn, Forest, Sunset };

struct Theme
{
    QString id; // changes whenever the look changes ("dark", "forest", "system-light"…)
    bool dark = false;
    QColor window;      // content area and its header bar
    QColor sidebar;     // sidebar and its header bar
    QColor outline;     // 1px window edge (client-side decorations)
    QColor fg;
    QColor dimFg;
    QColor hover;       // translucent overlays on top of a background
    QColor pressed;
    QColor selected;    // sidebar selection
    QColor accent;
    QColor itemSelected; // file grid/list selection (accent tint)
    QColor pathBg;      // path bar / search entry pill
    QColor button;      // round window-control buttons
    QColor buttonHover;
    QColor popup;       // menus, floating bar
    QColor border;
    QColor scroll;
    QColor scrollHover;

    static Theme fromPalette(const QPalette &palette); // the defaults, light or dark
    static Theme forMode(ThemeMode mode);
};

QList<ThemeMode> allThemeModes();
QString themeModeName(ThemeMode mode);

ThemeMode savedThemeMode();
// Apply `mode` to the whole application (sets QApplication's palette,
// which sends ApplicationPaletteChange to every widget). "System" keeps
// the platform palette.
void applyThemeMode(ThemeMode mode);
// Apply and remember for the next start.
void setThemeMode(ThemeMode mode);
// Tokens for what's applied now.
Theme currentTheme();

// Themes by key: the ones above ("system", "light", "dark", "midnight",
// "rose", "forest", "sunset") and the ones people make, which are files in
// a themes folder ("file:<name>"). docs/themes.md is the guide for writing
// one. This is what Preferences, the View menu and the settings use.
struct ThemeChoice
{
    QString key;
    QString name;
    QString file; // a theme from a file: where it is
};
QList<ThemeChoice> allThemes();
QString savedThemeKey();
QString currentThemeKey();
void applyThemeKey(const QString &key); // an unknown or broken one: Follow System
void setThemeKey(const QString &key);   // apply and remember
// The tokens of a theme, for a preview of it ("system": what it is now).
Theme themeForKey(const QString &key);

// Where people put theme files: $XDG_DATA_HOME/argonaut/themes.
QString userThemesFolder();
// Reads a theme file (JSON: a name, three or four colours, and any other
// colour to override). `problem` says what is wrong, if anything.
struct ThemeFile
{
    QString name;
    Theme theme;
    QString problem; // empty: fine
};
ThemeFile readThemeFile(const QString &path);
// Writes `theme` as a file in the user's folder, to start a new one from.
// Returns the file (never over an existing one), or empty and `error`.
QString saveThemeFile(const Theme &theme, const QString &name, QString *error = nullptr);

// "rgba(r, g, b, a)" for use in Qt style sheets.
QString cssColor(const QColor &color);

// Monochrome UI icons, drawn in code so they look the same on every
// platform and need no icon theme or SVG plugin.
enum class Glyph {
    Home, Desktop, Documents, Downloads, Music, Pictures, Videos, Trash,
    Drive, Removable,
    Search, Menu, Kebab, ChevronLeft, ChevronRight, ChevronUp, ChevronDown, Grid, List,
    Minimize, Maximize, Restore, Close, Terminal, Folder, Eject, Usage, Recent, Star, Split, Branch, Network, Cloud,
};

QIcon glyphIcon(Glyph glyph, const QColor &color);
// Argonaut's own icon (the same drawing as data/argonaut.svg), for windows.
QIcon applicationIcon();
