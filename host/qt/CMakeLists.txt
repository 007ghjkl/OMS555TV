cmake_minimum_required(VERSION 3.24)

project(OMS555TVHost VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Widgets Test SerialPort SerialBus)

include(CTest)

add_subdirectory(src)

add_executable(task008_vcp_integration
    tools/task008_vcp_integration.cpp
)

add_executable(task012_rs485_ui_validation
    tools/task012_rs485_ui_validation.cpp
)

add_executable(task013_rs485_validation
    tools/task013_rs485_validation.cpp
)
add_executable(task016_rs485_validation
    tools/task016_rs485_validation.cpp
)
add_executable(task020_rs485_validation
    tools/task020_rs485_validation.cpp
)
add_executable(task023_rs485_validation
    tools/task023_rs485_validation.cpp
)
target_link_libraries(task023_rs485_validation PRIVATE
    oms555tv_widgets
    Qt6::Widgets
    Qt6::SerialPort
)
target_link_libraries(task020_rs485_validation PRIVATE
    oms555tv_widgets
    Qt6::Widgets
    Qt6::SerialPort
)
target_link_libraries(task016_rs485_validation PRIVATE
    oms555tv_core
    Qt6::Core
    Qt6::SerialPort
)
target_link_libraries(task013_rs485_validation PRIVATE
    oms555tv_core
    Qt6::Core
    Qt6::SerialPort
)
target_link_libraries(task012_rs485_ui_validation PRIVATE
    oms555tv_widgets
    Qt6::Widgets
    Qt6::SerialPort
)
target_link_libraries(task008_vcp_integration PRIVATE
    oms555tv_core
    Qt6::Core
    Qt6::SerialPort
)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
