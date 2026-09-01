add_executable(tst_logentry
    logging/tst_logentry.cpp
)
target_link_libraries(tst_logentry PRIVATE oms555tv_core Qt6::Test)
add_test(NAME host.logging.logentry COMMAND tst_logentry)

add_executable(tst_qtserialbus_probe
    communication/tst_qtserialbus_probe.cpp
)
target_link_libraries(tst_qtserialbus_probe PRIVATE Qt6::Core Qt6::Test Qt6::SerialBus)
add_test(NAME host.technical.qtserialbus_raw_pdu COMMAND tst_qtserialbus_probe)

add_test(NAME host.application.smoke COMMAND oms555tv_host --smoke-test)
set_tests_properties(
    host.logging.logentry
    host.technical.qtserialbus_raw_pdu
    host.application.smoke
    PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)
set_tests_properties(
    host.application.smoke
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
