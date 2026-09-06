add_library(oms555tv_core STATIC
    app/AppStateController.cpp
    app/AppStateController.h
    app/AppStateTypes.h
    configuration/ConfigurationService.cpp
    configuration/ConfigurationService.h
    configuration/ConfigurationTypes.h
    diagnostics/CommunicationDiagnostics.cpp
    diagnostics/CommunicationDiagnostics.h
    logging/LogEntry.cpp
    logging/LogEntry.h
    logging/SessionLogService.cpp
    logging/SessionLogService.h
    communication/CommunicationTypes.cpp
    communication/CommunicationTypes.h
    communication/CommunicationWorker.cpp
    communication/CommunicationWorker.h
    communication/FakeModbusClient.cpp
    communication/FakeModbusClient.h
    communication/IModbusClient.h
    communication/ManualScheduler.cpp
    communication/ManualScheduler.h
    communication/QSerialPortModbusClient.cpp
    communication/QSerialPortModbusClient.h
    communication/QSerialPortTransport.cpp
    communication/QSerialPortTransport.h
    communication/RtuCodec.cpp
    communication/RtuCodec.h
    communication/SerialTransport.h
    device/DeviceTypes.h
    device/RegisterCodec.cpp
    device/RegisterCodec.h
    device/RegisterMap.h
    monitor/MonitorScheduler.cpp
    monitor/MonitorScheduler.h
    monitor/MonitorService.cpp
    monitor/MonitorService.h
    monitor/MonitorTypes.cpp
    monitor/MonitorTypes.h
    report/HtmlReportGenerator.cpp
    report/HtmlReportGenerator.h
    report/ReportModelBuilder.cpp
    report/ReportModelBuilder.h
    report/ReportTypes.cpp
    report/ReportTypes.h
    testing/TestAssertions.cpp
    testing/TestAssertions.h
    testing/GuidedTestModel.cpp
    testing/GuidedTestModel.h
    testing/GuidedTestCoordinator.cpp
    testing/GuidedTestCoordinator.h
    testing/TestAutomationController.cpp
    testing/TestAutomationController.h
    testing/TestCaseLoader.cpp
    testing/TestCaseLoader.h
    testing/TestCaseLoaderV2.cpp
    testing/TestCaseLoaderV2.h
    testing/TestCaseLoaderV3.cpp
    testing/TestCaseLoaderV3.h
    testing/TestCaseTypes.cpp
    testing/TestCaseTypes.h
    testing/TestCaseHandlers.cpp
    testing/TestCaseHandlers.h
    testing/TestEngine.cpp
    testing/TestEngine.h
    testing/TestEngineTypes.cpp
    testing/TestEngineTypes.h
    testing/TestResultManager.cpp
    testing/TestResultManager.h
    ui/MonitoringViewModel.cpp
    ui/MonitoringViewModel.h
    ui/SerialPortCatalog.cpp
    ui/SerialPortCatalog.h
)

target_include_directories(oms555tv_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(oms555tv_core PUBLIC Qt6::Core Qt6::SerialPort)

add_library(oms555tv_widgets STATIC
    app/MainWindow.cpp
    app/MainWindow.h
)

target_link_libraries(oms555tv_widgets PUBLIC
    oms555tv_core
    Qt6::Widgets
)

add_executable(oms555tv_host WIN32
    app/main.cpp
)

target_link_libraries(oms555tv_host PRIVATE oms555tv_widgets)
