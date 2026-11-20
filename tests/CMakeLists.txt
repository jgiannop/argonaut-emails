find_package(Qt6 REQUIRED COMPONENTS Test)

# mail_test(<name> [LIBRARY <lib>]): tests/tst_<name>.cpp as a CTest test.
# Every test runs headless, with its own HOME and XDG folders (TestSupport.h)
# and never the real keyring.
function(mail_test name)
    cmake_parse_arguments(ARG "" "LIBRARY" "" ${ARGN})
    if(NOT ARG_LIBRARY)
        set(ARG_LIBRARY mail_core)
    endif()
    qt_add_executable(tst_${name} tst_${name}.cpp TestSupport.h FakeServers.h)
    target_link_libraries(tst_${name} PRIVATE ${ARG_LIBRARY} Qt6::Test)
    add_test(NAME ${name} COMMAND tst_${name})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;MAIL_KEYSTORE=file"
        TIMEOUT 60)
endfunction()

mail_test(mime)
mail_test(imapparser)
mail_test(composer)
mail_test(store)
mail_test(smtp)
mail_test(sync)
mail_test(safehtml)
mail_test(mainwindow LIBRARY mail_app)
