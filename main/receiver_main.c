#include "freertos/FreeRTOS.h"
#include "lora_comm.h"
#include "oled_display.h"
#include "wifi_util.h"
#include <driver/gpio.h>
#include <esp_log.h>
#include <freertos/task.h>
#include <stdint.h>


lv_disp_t *disp;

int16_t humidity, temperature, rawSmoke, calSmokeVoltage = 0;

sx127x *device = NULL;
TaskHandle_t handle_interrupt;
int total_packets_received = 0;
static const char *TAG = "MAIN";

void app_main() {
  ESP_LOGI(TAG, "starting up");
  gpio_set_direction(RECEIVER_LED, GPIO_MODE_OUTPUT);
  setupOled();

  gpio_set_level(RECEIVER_LED, 0);
  setup_wifi();

  while (1) {
    vTaskDelay(10000 / portTICK_PERIOD_MS);
  }
}
