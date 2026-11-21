cmake_minimum_required(VERSION 3.21)
project(argonaut-emails VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 6.4 is what Ubuntu 24.04 ships, as for Argonaut.
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Network Sql WebEngineWidgets)
find_package(Qt6 6.4 QUIET COMPONENTS DBus)
qt_standard_project_setup()

include(GNUInstallDirs)

# The mail engine: no widgets, so all of it can be tested headless.
qt_add_library(mail_core STATIC
    src/core/Account.cpp src/core/Account.h
    src/core/Claude.cpp src/core/Claude.h
    src/core/ClaudeReports.cpp src/core/ClaudeReports.h
    src/core/Composer.cpp src/core/Composer.h
    src/core/ImapClient.cpp src/core/ImapClient.h
    src/core/Extensions.cpp src/core/Extensions.h
    src/core/ImapParser.cpp src/core/ImapParser.h
    src/core/MailReader.cpp src/core/MailReader.h
    src/core/MailStore.cpp src/core/MailStore.h
    src/core/Mime.cpp src/core/Mime.h
    src/core/Passwords.cpp src/core/Passwords.h
    src/core/SafeHtml.cpp src/core/SafeHtml.h
    src/core/SmtpClient.cpp src/core/SmtpClient.h
    src/core/Sync.cpp src/core/Sync.h
)
target_include_directories(mail_core PUBLIC src/core)
target_link_libraries(mail_core PUBLIC Qt6::Core Qt6::Network Qt6::Sql)
target_compile_definitions(mail_core PUBLIC MAIL_VERSION="${PROJECT_VERSION}")
if(Qt6DBus_FOUND AND UNIX AND NOT APPLE)
    target_link_libraries(mail_core PUBLIC Qt6::DBus)
    target_compile_definitions(mail_core PUBLIC MAIL_DBUS)
    message(STATUS "Passwords in the desktop keyring: enabled")
else()
    message(STATUS "Passwords in the desktop keyring: disabled (a private file instead)")
endif()

# The window.
qt_add_library(mail_app STATIC
    src/app/AccountDialog.cpp src/app/AccountDialog.h
    src/app/ClaudeDialog.cpp src/app/ClaudeDialog.h
    src/app/CommandPalette.cpp src/app/CommandPalette.h
    src/app/ComposeWindow.cpp src/app/ComposeWindow.h
    src/app/ExtensionForm.cpp src/app/ExtensionForm.h
    src/app/ExtensionMenu.cpp src/app/ExtensionMenu.h
    src/app/ExtensionsDialog.cpp src/app/ExtensionsDialog.h
    src/app/FolderTree.cpp src/app/FolderTree.h
    src/app/MailWebView.cpp src/app/MailWebView.h
    src/app/MainWindow.cpp src/app/MainWindow.h
    src/app/MainWindowExtensions.cpp
    src/app/MessageList.cpp src/app/MessageList.h
    src/app/MessageView.cpp src/app/MessageView.h
    src/app/RecentPopup.cpp src/app/RecentPopup.h
    src/app/StyleSheet.cpp src/app/StyleSheet.h
    src/app/Theme.cpp src/app/Theme.h
    src/app/TrayIcon.cpp src/app/TrayIcon.h
)
target_include_directories(mail_app PUBLIC src/app)
# Messages are shown by Chromium (Qt WebEngine), locked: see MailWebView.h.
target_link_libraries(mail_app PUBLIC mail_core Qt6::Widgets Qt6::WebEngineWidgets)

qt_add_executable(argonaut-emails src/app/main.cpp)
target_link_libraries(argonaut-emails PRIVATE mail_app)
install(TARGETS argonaut-emails RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(UNIX AND NOT APPLE)
    # The absolute path: the desktop doesn't use the user's PATH.
    set(MAIL_EXE "${CMAKE_INSTALL_FULL_BINDIR}/argonaut-emails")
    configure_file(data/argonaut-emails.desktop.in ${CMAKE_CURRENT_BINARY_DIR}/argonaut-emails.desktop @ONLY)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/argonaut-emails.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES data/argonaut-emails.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
    # Let menus and the dock see the new entry right away.
    install(CODE [[
        find_program(UPDATE_DESKTOP_DATABASE update-desktop-database)
        if(UPDATE_DESKTOP_DATABASE AND "$ENV{DESTDIR}" STREQUAL "")
            execute_process(COMMAND ${UPDATE_DESKTOP_DATABASE} "${CMAKE_INSTALL_PREFIX}/share/applications")
        endif()
    ]])
endif()
install(FILES LICENSE README.md DESTINATION ${CMAKE_INSTALL_DOCDIR})

# ---------------------------------------------------------- extensions ---
# Separate programs (src/core/Extensions.h, docs/extensions.md), as in
# Argonaut. Each comes with an extension.json next to the program in the
# build folder (found there when run from it) and one for the installed
# program in share/argonaut-emails/extensions.
qt_add_executable(argonaut-emails-week-extension extensions/week/main.cpp extensions/shared/Conversation.h)
target_link_libraries(argonaut-emails-week-extension PRIVATE mail_core)
set(MAIL_WEEK_EXTENSION_EXE "${CMAKE_CURRENT_BINARY_DIR}/argonaut-emails-week-extension")
configure_file(extensions/week/extension.json.in ${CMAKE_CURRENT_BINARY_DIR}/extensions/last-week/extension.json @ONLY)
set(MAIL_WEEK_EXTENSION_EXE "${CMAKE_INSTALL_FULL_LIBEXECDIR}/argonaut-emails/argonaut-emails-week-extension")
configure_file(extensions/week/extension.json.in ${CMAKE_CURRENT_BINARY_DIR}/install/last-week/extension.json @ONLY)
install(TARGETS argonaut-emails-week-extension RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/argonaut-emails)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/install/last-week/extension.json
        DESTINATION ${CMAKE_INSTALL_DATADIR}/argonaut-emails/extensions/last-week)

# A package for other people has every feature, or isn't made: without Qt
# D-Bus, passwords would go into a private file instead of the desktop's
# keyring, and nobody would notice. The `package` preset switches this on.
option(MAIL_REQUIRE_ALL_FEATURES "Fail when an optional feature can't be built" OFF)
if(MAIL_REQUIRE_ALL_FEATURES AND UNIX AND NOT APPLE AND NOT Qt6DBus_FOUND)
    message(FATAL_ERROR "This build is to have every feature (MAIL_REQUIRE_ALL_FEATURES), and Qt D-Bus is missing (passwords in the desktop keyring)")
endif()

# ------------------------------------------------------------- package ---
# A .deb for Ubuntu and Debian, as Argonaut's: `cmake --preset package &&
# cmake --build build/package && cpack --config build/package/CPackConfig.cmake`.
# Library dependencies are worked out by dpkg-shlibdeps; Qt's SQLite driver
# is a plugin, which it can't see, so it is named.
set(CPACK_PACKAGE_NAME "argonaut-emails")
set(CPACK_PACKAGE_VENDOR "johngiannop")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/jgiannop/argonaut-emails")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "An email client with Argonaut's look")
set(CPACK_PACKAGE_DESCRIPTION "Argonaut Emails is an email client written in C++ and Qt 6, the companion of \
the Argonaut file manager: IMAP and SMTP accounts (Gmail's inbox tabs too), three panes, messages \
shown locked (no scripts, no pictures from the internet until asked), passwords in the desktop's keyring.")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
# Into the build folder, not wherever cpack is run from.
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CPACK_GENERATOR "DEB")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "johngiannop")
set(CPACK_DEBIAN_PACKAGE_SECTION "mail")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/jgiannop/argonaut-emails")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libqt6sql6-sqlite")
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "qt6-wayland, gnome-keyring | kwalletmanager, xdg-utils")
include(CPack)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
