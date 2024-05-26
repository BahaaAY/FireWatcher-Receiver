#ifndef LORA_COMM_H
#define LORA_COMM_H

#include <sx127x.h>

#include "oled_display.h"
#include <driver/gpio.h>
#include <driver/spi_common.h>
#include <driver/spi_master.h>
#include <esp_err.h>
#include <esp_log.h>

// LORA PINS
#define SCK 5
#define MISO 19
#define MOSI 27
#define SS 18
#define RST 14
#define DIO0 26
#define DIO1 35
#define DIO2 34

#define RECEIVER_LED 33

void setup_lora();
void rx_callback(sx127x *device, uint8_t *data, uint16_t data_length);

void handle_interrupt_task(void *arg);

void setup_gpio_interrupts(gpio_num_t gpio);

void unpackData(const uint8_t *dataArray, int16_t *humidity,
                int16_t *temperature, int16_t *rawSmoke,
                int16_t *calSmokeVoltage);

#endif