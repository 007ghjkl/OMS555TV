cmake_minimum_required(VERSION 3.22)

project(oms555tv_firmware_phase1_tests LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

enable_testing()

set(APP_ROOT "${CMAKE_CURRENT_LIST_DIR}/../App")

add_library(phase1_core STATIC
    "${APP_ROOT}/alarm/alarm.c"
    "${APP_ROOT}/device_model/device_model.c"
    "${APP_ROOT}/acquisition/dhtc12_codec.c"
    "${APP_ROOT}/acquisition/acquisition.c"
    "${APP_ROOT}/modbus/modbus_crc.c"
    "${APP_ROOT}/modbus/modbus_rtu_rx.c"
    "${APP_ROOT}/modbus/register_map.c"
    "${APP_ROOT}/modbus/modbus_slave.c"
)

target_include_directories(phase1_core PUBLIC
    "${APP_ROOT}/alarm"
    "${APP_ROOT}/device_model"
    "${APP_ROOT}/acquisition"
    "${APP_ROOT}/platform"
    "${APP_ROOT}/modbus"
)

if(MSVC)
    target_compile_options(phase1_core PRIVATE /W4 /WX)
else()
    target_compile_options(phase1_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(phase1_unit_tests
    test_main.c
    test_alarm_model.c
    test_codec.c
    test_acquisition.c
    test_modbus_crc_rx.c
    test_register_map.c
    test_modbus_slave.c
)

target_link_libraries(phase1_unit_tests PRIVATE phase1_core)

if(MSVC)
    target_compile_options(phase1_unit_tests PRIVATE /W4 /WX)
else()
    target_compile_options(phase1_unit_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME phase1_unit_tests COMMAND phase1_unit_tests)
