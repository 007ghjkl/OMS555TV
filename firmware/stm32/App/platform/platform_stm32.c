#include "platform_stm32.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#include "main.h"

extern ADC_HandleTypeDef hadc1;
extern I2C_HandleTypeDef hi2c1;
extern I2C_HandleTypeDef hi2c2;
extern I2C_HandleTypeDef hi2c3;
extern UART_HandleTypeDef huart2;

typedef struct {
    I2C_HandleTypeDef *handle;
    GPIO_TypeDef *scl_port;
    uint16_t scl_pin;
    GPIO_TypeDef *sda_port;
    uint16_t sda_pin;
} DhtBus;

static bool dht_bus_for_channel(TemperatureChannel channel, DhtBus *bus)
{
    if (bus == NULL) {
        return false;
    }

    switch (channel) {
    case TEMP_CHANNEL_A:
        bus->handle = &hi2c1;
        bus->scl_port = GPIOB;
        bus->scl_pin = GPIO_PIN_8;
        bus->sda_port = GPIOB;
        bus->sda_pin = GPIO_PIN_9;
        return true;
    case TEMP_CHANNEL_B:
        bus->handle = &hi2c2;
        bus->scl_port = GPIOB;
        bus->scl_pin = GPIO_PIN_10;
        bus->sda_port = GPIOB;
        bus->sda_pin = GPIO_PIN_3;
        return true;
    case TEMP_CHANNEL_C:
        bus->handle = &hi2c3;
        bus->scl_port = GPIOA;
        bus->scl_pin = GPIO_PIN_8;
        bus->sda_port = GPIOC;
        bus->sda_pin = GPIO_PIN_9;
        return true;
    default:
        memset(bus, 0, sizeof(*bus));
        return false;
    }
}

static I2C_HandleTypeDef *i2c_for_channel(TemperatureChannel channel)
{
    DhtBus bus;
    return dht_bus_for_channel(channel, &bus) ? bus.handle : NULL;
}

static void recovery_delay(void)
{
    volatile uint32_t iterations = SystemCoreClock / 100000u;

    if (iterations == 0u) {
        iterations = 1u;
    }
    while (iterations-- > 0u) {
        __NOP();
    }
}

static PlatformStatus stm32_dht_recover(void *context,
                                        TemperatureChannel channel)
{
    DhtBus bus;
    GPIO_InitTypeDef gpio = {0};
    unsigned int pulse;
    bool released;
    (void)context;

    if (!dht_bus_for_channel(channel, &bus)) {
        return PLATFORM_INVALID_ARGUMENT;
    }

    if (HAL_GPIO_ReadPin(bus.scl_port, bus.scl_pin) == GPIO_PIN_SET &&
        HAL_GPIO_ReadPin(bus.sda_port, bus.sda_pin) == GPIO_PIN_SET) {
        return PLATFORM_OK;
    }

    if (HAL_I2C_DeInit(bus.handle) != HAL_OK) {
        return PLATFORM_IO_ERROR;
    }

    HAL_GPIO_WritePin(bus.scl_port, bus.scl_pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(bus.sda_port, bus.sda_pin, GPIO_PIN_SET);
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Pin = bus.scl_pin;
    HAL_GPIO_Init(bus.scl_port, &gpio);
    gpio.Pin = bus.sda_pin;
    HAL_GPIO_Init(bus.sda_port, &gpio);

    for (pulse = 0u;
         pulse < 9u &&
         HAL_GPIO_ReadPin(bus.sda_port, bus.sda_pin) == GPIO_PIN_RESET;
         ++pulse) {
        HAL_GPIO_WritePin(bus.scl_port, bus.scl_pin, GPIO_PIN_RESET);
        recovery_delay();
        HAL_GPIO_WritePin(bus.scl_port, bus.scl_pin, GPIO_PIN_SET);
        recovery_delay();
    }

    HAL_GPIO_WritePin(bus.sda_port, bus.sda_pin, GPIO_PIN_RESET);
    recovery_delay();
    HAL_GPIO_WritePin(bus.scl_port, bus.scl_pin, GPIO_PIN_SET);
    recovery_delay();
    HAL_GPIO_WritePin(bus.sda_port, bus.sda_pin, GPIO_PIN_SET);
    recovery_delay();
    released =
        HAL_GPIO_ReadPin(bus.scl_port, bus.scl_pin) == GPIO_PIN_SET &&
        HAL_GPIO_ReadPin(bus.sda_port, bus.sda_pin) == GPIO_PIN_SET;

    if (HAL_I2C_Init(bus.handle) != HAL_OK) {
        return PLATFORM_IO_ERROR;
    }
    return released ? PLATFORM_OK : PLATFORM_BUS_ERROR;
}

static PlatformStatus map_i2c_status(I2C_HandleTypeDef *handle,
                                     HAL_StatusTypeDef status,
                                     bool nack_means_not_ready)
{
    if (status == HAL_OK) {
        return PLATFORM_OK;
    }
    if (status == HAL_TIMEOUT) {
        return PLATFORM_TIMEOUT;
    }
    if (nack_means_not_ready && handle != NULL &&
        (HAL_I2C_GetError(handle) & HAL_I2C_ERROR_AF) != 0u) {
        return PLATFORM_I2C_NOT_READY;
    }
    return PLATFORM_BUS_ERROR;
}

static PlatformStatus stm32_dht_write(void *context,
                                      TemperatureChannel channel,
                                      uint8_t address_7bit,
                                      const uint8_t *data,
                                      size_t length,
                                      uint32_t timeout_ms)
{
    I2C_HandleTypeDef *handle = i2c_for_channel(channel);
    HAL_StatusTypeDef status;
    (void)context;

    if (handle == NULL || data == NULL || length == 0u ||
        length > UINT16_MAX || address_7bit > 0x7Fu) {
        return PLATFORM_INVALID_ARGUMENT;
    }

    status = HAL_I2C_Master_Transmit(handle,
                                     (uint16_t)((uint16_t)address_7bit << 1u),
                                     (uint8_t *)data,
                                     (uint16_t)length,
                                     timeout_ms);
    return map_i2c_status(handle, status, false);
}

static PlatformStatus stm32_dht_read(void *context,
                                     TemperatureChannel channel,
                                     uint8_t address_7bit,
                                     uint8_t *data,
                                     size_t capacity,
                                     size_t *received_length,
                                     uint32_t timeout_ms)
{
    I2C_HandleTypeDef *handle = i2c_for_channel(channel);
    HAL_StatusTypeDef status;
    (void)context;

    if (handle == NULL || data == NULL || received_length == NULL ||
        capacity == 0u || capacity > UINT16_MAX || address_7bit > 0x7Fu) {
        return PLATFORM_INVALID_ARGUMENT;
    }

    *received_length = 0u;
    status = HAL_I2C_Master_Receive(handle,
                                    (uint16_t)((uint16_t)address_7bit << 1u),
                                    data,
                                    (uint16_t)capacity,
                                    timeout_ms);
    if (status == HAL_OK) {
        *received_length = capacity;
    }
    return map_i2c_status(handle, status, true);
}

static PlatformStatus stm32_adc_start(void *context)
{
    (void)context;
    return HAL_ADC_Start(&hadc1) == HAL_OK
        ? PLATFORM_OK
        : PLATFORM_IO_ERROR;
}

static PlatformStatus stm32_adc_read(void *context,
                                     uint16_t *raw,
                                     uint32_t timeout_ms)
{
    HAL_StatusTypeDef status;
    uint32_t value;
    (void)context;

    if (raw == NULL) {
        return PLATFORM_INVALID_ARGUMENT;
    }

    status = HAL_ADC_PollForConversion(&hadc1, timeout_ms);
    if (status != HAL_OK) {
        (void)HAL_ADC_Stop(&hadc1);
        return status == HAL_TIMEOUT ? PLATFORM_TIMEOUT : PLATFORM_IO_ERROR;
    }

    value = HAL_ADC_GetValue(&hadc1);
    (void)HAL_ADC_Stop(&hadc1);
    if (value > UINT16_MAX) {
        return PLATFORM_IO_ERROR;
    }
    *raw = (uint16_t)value;
    return PLATFORM_OK;
}

static PlatformStatus stm32_debug_write(void *context,
                                        const uint8_t *data,
                                        size_t length)
{
    (void)context;
    if (data == NULL || length == 0u || length > UINT16_MAX) {
        return PLATFORM_INVALID_ARGUMENT;
    }
    return HAL_UART_Transmit(&huart2,
                             (uint8_t *)data,
                             (uint16_t)length,
                             50u) == HAL_OK
        ? PLATFORM_OK
        : PLATFORM_IO_ERROR;
}

static uint32_t stm32_millis(void *context)
{
    (void)context;
    return HAL_GetTick();
}

bool platform_stm32_create(Phase1Platform *platform)
{
    if (platform == NULL) {
        return false;
    }

    memset(platform, 0, sizeof(*platform));
    platform->dht_recover = stm32_dht_recover;
    platform->dht_write = stm32_dht_write;
    platform->dht_read = stm32_dht_read;
    platform->adc_start = stm32_adc_start;
    platform->adc_read = stm32_adc_read;
    platform->debug_write = stm32_debug_write;
    platform->millis = stm32_millis;
    return true;
}
