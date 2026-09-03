add_library(oms555tv_core STATIC
    logging/LogEntry.cpp
    logging/LogEntry.h
    communication/IModbusClient.h
    device/DeviceTypes.h
    device/RegisterCodec.cpp
    device/RegisterCodec.h
    device/RegisterMap.h
)

target_include_directories(oms555tv_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(oms555tv_core PUBLIC Qt6::Core)

add_executable(oms555tv_host WIN32
    app/main.cpp
    app/MainWindow.cpp
    app/MainWindow.h
)

target_link_libraries(oms555tv_host PRIVATE
    oms555tv_core
    Qt6::Widgets
)
