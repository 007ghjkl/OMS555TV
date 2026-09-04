add_executable(tst_logentry
    logging/tst_logentry.cpp
)
target_link_libraries(tst_logentry PRIVATE oms555tv_core Qt6::Test)
add_test(NAME host.logging.logentry COMMAND tst_logentry)

add_executable(tst_sessionlog
    logging/tst_sessionlog.cpp
)
target_link_libraries(tst_sessionlog PRIVATE oms555tv_core Qt6::Test)
add_test(NAME host.logging.session COMMAND tst_sessionlog)

add_executable(tst_qtserialbus_probe
    communication/tst_qtserialbus_probe.cpp
)
target_link_libraries(tst_qtserialbus_probe PRIVATE Qt6::Core Qt6::Test Qt6::SerialBus)
add_test(NAME host.technical.qtserialbus_raw_pdu COMMAND tst_qtserialbus_probe)

add_executable(tst_qserialport_rtu_probe
    communication/tst_qserialport_rtu_probe.cpp
)
target_link_libraries(tst_qserialport_rtu_probe PRIVATE Qt6::Core Qt6::Test Qt6::SerialPort)
add_test(NAME host.technical.qserialport_rtu_public_api COMMAND tst_qserialport_rtu_probe)

add_executable(tst_registercodec
    device/tst_registercodec.cpp
)
target_link_libraries(tst_registercodec PRIVATE oms555tv_core Qt6::Test)
add_test(NAME host.device.registercodec COMMAND tst_registercodec)

add_executable(tst_communicationtypes
    communication/tst_communicationtypes.cpp
)
target_link_libraries(tst_communicationtypes PRIVATE oms555tv_core Qt6::Test)
add_test(NAME host.communication.types COMMAND tst_communicationtypes)

add_executable(tst_rtucodec
    communication/tst_rtucodec.cpp
)
target_link_libraries(tst_rtucodec PRIVATE oms555tv_core Qt6::Test)
add_test(NAME host.communication.rtu_codec COMMAND tst_rtucodec)

add_executable(tst_fakemodbusclient
    communication/IModbusClientContract.h
    communication/tst_fakemodbusclient.cpp
)
target_link_libraries(tst_fakemodbusclient PRIVATE oms555tv_core Qt6::Test)
add_test(NAME host.communication.fake_contract COMMAND tst_fakemodbusclient)

add_executable(tst_configuration
    monitor/ManualMonitorScheduler.h
    configuration/tst_configuration.cpp
)
target_link_libraries(tst_configuration PRIVATE oms555tv_core Qt6::Test)
add_test(NAME host.configuration.service COMMAND tst_configuration)

add_executable(tst_diagnostics
    diagnostics/tst_diagnostics.cpp
)
target_link_libraries(tst_diagnostics PRIVATE oms555tv_core Qt6::Test)
add_test(NAME host.communication.diagnostics COMMAND tst_diagnostics)

add_executable(tst_qserialportmodbusclient
    communication/IModbusClientContract.h
    communication/tst_qserialportmodbusclient.cpp
)
target_link_libraries(tst_qserialportmodbusclient PRIVATE oms555tv_core Qt6::Test)
add_test(NAME host.communication.qserialport_backend COMMAND tst_qserialportmodbusclient)

add_executable(tst_monitoringcore
    monitor/ManualMonitorScheduler.h
    monitor/tst_monitoringcore.cpp
)
target_link_libraries(tst_monitoringcore PRIVATE oms555tv_core Qt6::Test)
add_test(NAME host.monitoring.core COMMAND tst_monitoringcore)

add_executable(tst_monitoringui
    ui/tst_monitoringui.cpp
)
target_link_libraries(tst_monitoringui PRIVATE oms555tv_widgets Qt6::Test)
add_test(NAME host.monitoring.ui COMMAND tst_monitoringui)

add_executable(tst_configurationdiagnosticsui
    monitor/ManualMonitorScheduler.h
    ui/tst_configurationdiagnosticsui.cpp
)
target_link_libraries(tst_configurationdiagnosticsui PRIVATE oms555tv_widgets Qt6::Test)
add_test(NAME host.configuration_diagnostics.ui COMMAND tst_configurationdiagnosticsui)

add_test(NAME host.application.smoke COMMAND oms555tv_host --smoke-test)
set_tests_properties(
    host.logging.logentry
    host.logging.session
    host.technical.qtserialbus_raw_pdu
    host.technical.qserialport_rtu_public_api
    host.device.registercodec
    host.communication.types
    host.communication.rtu_codec
    host.communication.fake_contract
    host.configuration.service
    host.communication.diagnostics
    host.communication.qserialport_backend
    host.monitoring.core
    host.monitoring.ui
    host.configuration_diagnostics.ui
    host.application.smoke
    PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)
set_tests_properties(
    host.monitoring.ui
    host.configuration_diagnostics.ui
    host.application.smoke
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
