cmake_minimum_required(VERSION 3.21)
project(argonaut-emails VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 6.4 is what Ubuntu 24.04 ships, as for Argonaut.
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Network Sql)
find_package(Qt6 6.4 QUIET COMPONENTS DBus)
qt_standard_project_setup()

include(GNUInstallDirs)

# The mail engine: no widgets, so all of it can be tested headless.
qt_add_library(mail_core STATIC
    src/core/Account.cpp src/core/Account.h
    src/core/Composer.cpp src/core/Composer.h
    src/core/ImapClient.cpp src/core/ImapClient.h
    src/core/ImapParser.cpp src/core/ImapParser.h
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
    src/app/ComposeWindow.cpp src/app/ComposeWindow.h
    src/app/FolderTree.cpp src/app/FolderTree.h
    src/app/MainWindow.cpp src/app/MainWindow.h
    src/app/MessageList.cpp src/app/MessageList.h
    src/app/MessageView.cpp src/app/MessageView.h
    src/app/StyleSheet.cpp src/app/StyleSheet.h
    src/app/Theme.cpp src/app/Theme.h
)
target_include_directories(mail_app PUBLIC src/app)
target_link_libraries(mail_app PUBLIC mail_core Qt6::Widgets)

qt_add_executable(argonaut-emails src/app/main.cpp)
target_link_libraries(argonaut-emails PRIVATE mail_app)
install(TARGETS argonaut-emails RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(UNIX AND NOT APPLE)
    # The absolute path: the desktop doesn't use the user's PATH.
    set(MAIL_EXE "${CMAKE_INSTALL_FULL_BINDIR}/argonaut-emails")
    configure_file(data/argonaut-emails.desktop.in ${CMAKE_CURRENT_BINARY_DIR}/argonaut-emails.desktop @ONLY)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/argonaut-emails.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
endif()

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
