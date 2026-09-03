#include "app.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "acquisition.h"
#include "modbus_rtu_rx.h"
#include "modbus_slave.h"
#include "platform_stm32.h"

#ifndef DHTC12_TEMPERATURE_CONVERSION_CONFIRMED
#define DHTC12_TEMPERATURE_CONVERSION_CONFIRMED 0
#endif

#ifndef PHASE1_DEBUG_LOG
#define PHASE1_DEBUG_LOG 0
#endif

#define LOG_BUFFER_SIZE 256u

typedef struct {
    DeviceModel model;
    AcquisitionContext acquisition;
    Phase1Platform platform;
    ModbusPlatform modbus;
    uint8_t modbus_request[MODBUS_RTU_MAX_ADU];
    uint8_t modbus_response[MODBUS_RTU_MAX_ADU];
    uint32_t last_uptime_tick_ms;
    uint16_t uptime_remainder_ms;
    bool initialized;
} AppContext;

static AppContext g_app;

#if PHASE1_DEBUG_LOG
static void debug_line(const char *line, int length)
{
    if (line == NULL || length <= 0 || g_app.platform.debug_write == NULL) {
        return;
    }
    if ((size_t)length >= LOG_BUFFER_SIZE) {
        length = (int)(LOG_BUFFER_SIZE - 1u);
    }
    (void)g_app.platform.debug_write(g_app.platform.context,
                                     (const uint8_t *)line,
                                     (size_t)length);
}

static const char *crc_label(bool available, bool valid)
{
    if (!available) {
        return "NA";
    }
    return valid ? "OK" : "FAIL";
}

static void log_dht_event(const AcquisitionEvent *event,
                          const DeviceModel *model)
{
    char line[LOG_BUFFER_SIZE];
    const TemperatureChannel channel = event->data.dht.channel;
    const TemperatureState *state = &model->temperatures[channel];
    const bool frame_available = event->data.dht.frame_available;
    int length;

    if (frame_available) {
        const Dhtc12Frame *frame = &event->data.dht.frame;
        length = snprintf(
            line,
            sizeof(line),
            "seq=%lu ms=%lu ch=%s op=%s elapsed_ms=%lu "
            "raw_t=%04X raw_h=%04X "
            "crc_t=%s crc_h=%s temperature=%d source=DHTC12 valid=%u "
            "error=%s crc_fail=%u invalid=%u alarm=0x%04X status=0x%04X\r\n",
            (unsigned long)event->sequence,
            (unsigned long)event->timestamp_ms,
            temperature_channel_name(channel),
            event->data.dht.initialization ? "INIT" : "MEASURE",
            (unsigned long)event->data.dht.conversion_elapsed_ms,
            (unsigned int)frame->temperature_raw,
            (unsigned int)frame->humidity_raw,
            crc_label(true, frame->temperature_crc_valid),
            crc_label(true, frame->humidity_crc_valid),
            (int)frame->temperature_deci_c,
            state->valid ? 1u : 0u,
            acquisition_error_name(event->error),
            (unsigned int)state->consecutive_crc_failures,
            (unsigned int)state->consecutive_invalid_cycles,
            (unsigned int)model->alarm_bits,
            (unsigned int)model->status_bits);
    } else {
        length = snprintf(
            line,
            sizeof(line),
            "seq=%lu ms=%lu ch=%s op=%s elapsed_ms=%lu "
            "raw_t=NA raw_h=NA "
            "crc_t=NA crc_h=NA "
            "temperature=NA source=NA valid=%u error=%s platform=%u "
            "crc_fail=%u invalid=%u alarm=0x%04X status=0x%04X\r\n",
            (unsigned long)event->sequence,
            (unsigned long)event->timestamp_ms,
            temperature_channel_name(channel),
            event->data.dht.initialization ? "INIT" : "MEASURE",
            (unsigned long)event->data.dht.conversion_elapsed_ms,
            state->valid ? 1u : 0u,
            acquisition_error_name(event->error),
            (unsigned int)event->platform_status,
            (unsigned int)state->consecutive_crc_failures,
            (unsigned int)state->consecutive_invalid_cycles,
            (unsigned int)model->alarm_bits,
            (unsigned int)model->status_bits);
    }

    debug_line(line, length);
}

static void log_light_event(const AcquisitionEvent *event,
                            const DeviceModel *model)
{
    char line[LOG_BUFFER_SIZE];
    const int length = snprintf(
        line,
        sizeof(line),
        "seq=%lu ms=%lu ch=LIGHT raw_avg=%u samples=%u value_mv=%u "
        "valid=%u error=%s platform=%u alarm=0x%04X status=0x%04X\r\n",
        (unsigned long)event->sequence,
        (unsigned long)event->timestamp_ms,
        (unsigned int)event->data.light.average_raw,
        (unsigned int)event->data.light.sample_count,
        (unsigned int)event->data.light.value_mv,
        model->light.valid ? 1u : 0u,
        acquisition_error_name(event->error),
        (unsigned int)event->platform_status,
        (unsigned int)model->alarm_bits,
        (unsigned int)model->status_bits);

    debug_line(line, length);
}

static void acquisition_observer(void *context,
                                 const AcquisitionEvent *event,
                                 const DeviceModel *model)
{
    (void)context;
    if (event == NULL || model == NULL) {
        return;
    }
    if (event->type == ACQUISITION_EVENT_DHT) {
        log_dht_event(event, model);
    } else {
        log_light_event(event, model);
    }
}

static void log_boot(uint32_t now_ms)
{
    char line[LOG_BUFFER_SIZE];
    int length = snprintf(
        line,
        sizeof(line),
        "boot ms=%lu fw=%u.%u conversion_confirmed=%u status=0x%04X\r\n",
        (unsigned long)now_ms,
        (unsigned int)g_app.model.firmware_version_major,
        (unsigned int)g_app.model.firmware_version_minor,
        DHTC12_TEMPERATURE_CONVERSION_CONFIRMED != 0 ? 1u : 0u,
        (unsigned int)g_app.model.status_bits);
    debug_line(line, length);

    length = snprintf(
        line,
        sizeof(line),
        "seq=INIT ms=%lu ch=AMBIENT source=SIMULATED value=250 valid=1 "
        "alarm=0x%04X status=0x%04X\r\n",
        (unsigned long)now_ms,
        (unsigned int)g_app.model.alarm_bits,
        (unsigned int)g_app.model.status_bits);
    debug_line(line, length);
}
#endif

static void update_uptime(uint32_t now_ms)
{
    const uint32_t delta_ms = now_ms - g_app.last_uptime_tick_ms;
    uint32_t seconds = delta_ms / 1000u;
    uint32_t remainder = delta_ms % 1000u;

    g_app.last_uptime_tick_ms = now_ms;
    remainder += g_app.uptime_remainder_ms;
    seconds += remainder / 1000u;
    g_app.uptime_remainder_ms = (uint16_t)(remainder % 1000u);
    g_app.model.uptime_seconds += seconds;
}

static void service_modbus(uint32_t now_ms)
{
    ModbusPortEvent port_event;
    size_t request_length = 0u;

    port_event = g_app.modbus.poll(g_app.modbus.context,
                                   now_ms,
                                   g_app.modbus_request,
                                   sizeof(g_app.modbus_request),
                                   &request_length);
    if (port_event.type == MODBUS_PORT_EVENT_UART_ERROR) {
        device_model_add_communication_errors(
            &g_app.model, port_event.uart_error_count);
        return;
    }
    if (port_event.type == MODBUS_PORT_EVENT_OVERFLOW) {
        device_model_add_communication_errors(&g_app.model, 1u);
        return;
    }
    if (port_event.type == MODBUS_PORT_EVENT_FRAME) {
        const ModbusProcessOutcome result = modbus_slave_process_adu(
            &g_app.model,
            g_app.modbus_request,
            request_length,
            g_app.modbus_response,
            sizeof(g_app.modbus_response));

        if (result.result == MODBUS_PROCESS_COMMUNICATION_ERROR) {
            device_model_add_communication_errors(&g_app.model, 1u);
        } else if (result.result == MODBUS_PROCESS_RESPONSE_READY) {
            (void)g_app.modbus.transmit(g_app.modbus.context,
                                        g_app.modbus_response,
                                        result.response_length);
        }
    }
}

bool app_init(void)
{
    uint32_t now_ms;

    if (!platform_stm32_create(&g_app.platform) ||
        !platform_stm32_create_modbus(&g_app.modbus) ||
        g_app.platform.millis == NULL) {
        return false;
    }

    device_model_init(&g_app.model);
    now_ms = g_app.platform.millis(g_app.platform.context);
    if (!acquisition_init(
            &g_app.acquisition,
            &g_app.platform,
#if PHASE1_DEBUG_LOG
            acquisition_observer,
#else
            NULL,
#endif
            NULL,
            DHTC12_TEMPERATURE_CONVERSION_CONFIRMED != 0,
            now_ms)) {
        return false;
    }
    if (g_app.modbus.start == NULL || g_app.modbus.poll == NULL ||
        g_app.modbus.transmit == NULL ||
        !g_app.modbus.start(g_app.modbus.context)) {
        return false;
    }

    g_app.last_uptime_tick_ms = now_ms;
    g_app.uptime_remainder_ms = 0u;
    g_app.initialized = true;
    device_model_set_running(&g_app.model, true);
#if PHASE1_DEBUG_LOG
    log_boot(now_ms);
#endif
    return true;
}

void app_service(void)
{
    uint32_t now_ms;

    if (!g_app.initialized) {
        return;
    }

    now_ms = g_app.platform.millis(g_app.platform.context);
    update_uptime(now_ms);
    service_modbus(now_ms);
    acquisition_service(&g_app.acquisition, &g_app.model, now_ms);
}

const DeviceModel *app_device_model(void)
{
    return g_app.initialized ? &g_app.model : NULL;
}
