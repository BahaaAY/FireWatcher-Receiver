#include "freertos/FreeRTOS.h"
#include "lora_comm.h"
#include "oled_display.h"
#include "wifi_util.h"
#include <driver/gpio.h>
#include <esp_log.h>
#include <freertos/task.h>
#include <stdint.h>

#include "http_util.h"

#include "sensor_data.h"

lv_disp_t *disp;

int16_t humidity, temperature, rawSmoke, calSmokeVoltage = 0;

sx127x *device = NULL;
TaskHandle_t handle_interrupt;
int total_packets_received = 0;
static const char *TAG = "MAIN";

QueueHandle_t readings_queue;

void app_main() {
  ESP_LOGI(TAG, "starting up");
  gpio_set_direction(RECEIVER_LED, GPIO_MODE_OUTPUT);
  gpio_set_direction(WIFI_NOT_PROV_PIN, GPIO_MODE_OUTPUT);
  gpio_set_direction(WIFI_PROV_PIN, GPIO_MODE_OUTPUT);

  readings_queue = xQueueCreate(10, sizeof(SensorData));

  setupOled();
  gpio_set_level(RECEIVER_LED, 0);
  setup_wifi();

  xTaskCreate(send_data_to_server_task, "send_data_to_server_task", 4096, NULL,
              5, NULL);

  while (1) {
    ESP_LOGI(TAG, "MAIN LOOP");
    vTaskDelay(10000 / portTICK_PERIOD_MS);
  }
}
