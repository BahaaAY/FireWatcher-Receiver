#include <driver/gpio.h>
#include <driver/spi_common.h>
#include <driver/spi_master.h>
#include <esp_intr_alloc.h>
#include <esp_log.h>
#include <freertos/task.h>
#include <inttypes.h>
#include <sx127x.h>

#include "driver/i2c_master.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"

#include "esp_lvgl_port.h"
#include "lvgl.h"

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

#define I2C_BUS_PORT 0

#define OLED_PIXEL_CLOCK_HZ (400 * 1000)
#define OLED_PIN_NUM_SDA 4
#define OLED_PIN_NUM_SCL 15
#define OLED_PIN_NUM_RST 16
#define OLED_I2C_HW_ADDR 0x3C

#define OLED_H_RES 128
#define OLED_V_RES 64

#define OLED_CMD_BITS 8
#define OLED_PARAM_BITS 8
lv_disp_t *disp;

int16_t humidity, temperature, rawSmoke, calSmokeVoltage;

#include <stdint.h>

void unpackData(const uint8_t *dataArray, int16_t *humidity,
                int16_t *temperature, int16_t *rawSmoke,
                int16_t *calSmokeVoltage) {
  // Unpack humidity
  *humidity = (int16_t)((dataArray[1] << 8) | dataArray[0]);

  // Unpack temperature
  *temperature = (int16_t)((dataArray[3] << 8) | dataArray[2]);

  // Unpack rawSmoke
  *rawSmoke = (int16_t)((dataArray[5] << 8) | dataArray[4]);

  // Unpack calSmokeVoltage
  *calSmokeVoltage = (int16_t)((dataArray[7] << 8) | dataArray[6]);
}

sx127x *device = NULL;
TaskHandle_t handle_interrupt;
int total_packets_received = 0;
static const char *TAG = "sx127x";

void IRAM_ATTR handle_interrupt_fromisr(void *arg) {
  xTaskResumeFromISR(handle_interrupt);
}

void handle_interrupt_task(void *arg) {
  while (1) {
    vTaskSuspend(NULL);
    sx127x_handle_interrupt((sx127x *)arg);
  }
}

void display_oled(int16_t *temperature, int16_t *humidity, int16_t *smoke,
                  int16_t *calSmokeVoltage) {

  lv_obj_t *scr = lv_disp_get_scr_act(disp);

  // clear the screen

  lv_obj_clean(scr);

  lv_obj_t *label = lv_label_create(scr);
  // lv_label_set_long_mode(label,
  //                        LV_LABEL_LONG_SCROLL_CIRCULAR); /* Circular scroll
  //                        */
  lv_label_set_text_fmt(
      label, "Temperature:%dC\nHumidity: %d%%\nSmoke: %d\nVoltage: %d\n",
      *temperature, *humidity, *smoke, *calSmokeVoltage);
  // lv_label_set_text_fmt(label, "Humidity: %.2f%%\n", *humidity);

  /* Size of the screen (if you use rotation 90 or 270, please set
   * disp->driver->ver_res) */
  lv_obj_set_width(label, disp->driver->hor_res);
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 0);
}

void rx_callback(sx127x *device, uint8_t *data, uint16_t data_length) {
  gpio_set_level(RECEIVER_LED, 1);
  uint8_t payload[514];
  const char SYMBOLS[] = "0123456789ABCDEF";
  for (size_t i = 0; i < data_length; i++) {
    uint8_t cur = data[i];
    payload[2 * i] = SYMBOLS[cur >> 4];
    payload[2 * i + 1] = SYMBOLS[cur & 0x0F];
  }
  payload[data_length * 2] = '\0';

  int32_t frequency_error;
  ESP_ERROR_CHECK(sx127x_rx_get_frequency_error(device, &frequency_error));
  ESP_LOGI(TAG, "received: %d %s freq_error: %" PRId32, data_length, payload,
           frequency_error);
  // print rssi
  int16_t rssi;
  ESP_ERROR_CHECK(sx127x_rx_get_packet_rssi(device, &rssi));
  ESP_LOGI(TAG, "with rssi: %d", rssi);

  unpackData(data, &humidity, &temperature, &rawSmoke, &calSmokeVoltage);
  ESP_LOGI(TAG, "Humidity: %d%%", humidity);
  ESP_LOGI(TAG, "Temperature: %dC", temperature);
  ESP_LOGI(TAG, "Raw Smoke: %d", rawSmoke);
  ESP_LOGI(TAG, "Calibrated Smoke: %d", calSmokeVoltage);

  ESP_LOGI(TAG, "total packets received: %d", total_packets_received);

  total_packets_received++;
  display_oled(&temperature, &humidity, &rawSmoke, &calSmokeVoltage);
  gpio_set_level(RECEIVER_LED, 0);
}

void setup_gpio_interrupts(gpio_num_t gpio, sx127x *device) {
  gpio_set_direction(gpio, GPIO_MODE_INPUT);
  gpio_pulldown_en(gpio);
  gpio_pullup_dis(gpio);
  gpio_set_intr_type(gpio, GPIO_INTR_POSEDGE);
  gpio_isr_handler_add(gpio, handle_interrupt_fromisr, (void *)device);
}

void setupOled() {
  TAG = "SETUP_OLED";
  ESP_LOGI(TAG, "Initialize I2C bus");
  i2c_master_bus_handle_t i2c_bus = NULL;
  i2c_master_bus_config_t bus_config = {
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .glitch_ignore_cnt = 7,
      .i2c_port = I2C_BUS_PORT,
      .sda_io_num = OLED_PIN_NUM_SDA,
      .scl_io_num = OLED_PIN_NUM_SCL,
      .flags.enable_internal_pullup = true,
  };
  ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &i2c_bus));

  ESP_LOGI(TAG, "Install panel IO");
  esp_lcd_panel_io_handle_t io_handle = NULL;
  esp_lcd_panel_io_i2c_config_t io_config = {
      .dev_addr = OLED_I2C_HW_ADDR,
      .scl_speed_hz = OLED_PIXEL_CLOCK_HZ,
      .control_phase_bytes = 1,        // According to SSD1306 datasheet
      .lcd_cmd_bits = OLED_CMD_BITS,   // According to SSD1306 datasheet
      .lcd_param_bits = OLED_CMD_BITS, // According to SSD1306 datasheet
      .dc_bit_offset = 6,              // According to SSD1306 datasheet
  };

  ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus, &io_config, &io_handle));
  ESP_LOGI(TAG, "Install SSD1306 panel driver");
  esp_lcd_panel_handle_t panel_handle = NULL;
  esp_lcd_panel_dev_config_t panel_config = {
      .bits_per_pixel = 1,
      .reset_gpio_num = OLED_PIN_NUM_RST,
  };
  ESP_ERROR_CHECK(
      esp_lcd_new_panel_ssd1306(io_handle, &panel_config, &panel_handle));

  ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

  // setup lvgl
  TAG = "LVGL_SETUP";
  ESP_LOGI(TAG, "Initialize LVGL");
  const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
  lvgl_port_init(&lvgl_cfg);

  const lvgl_port_display_cfg_t disp_cfg = {.io_handle = io_handle,
                                            .panel_handle = panel_handle,
                                            .buffer_size =
                                                OLED_H_RES * OLED_V_RES,
                                            .double_buffer = true,
                                            .hres = OLED_H_RES,
                                            .vres = OLED_V_RES,
                                            .monochrome = true,
                                            .rotation = {
                                                .swap_xy = false,
                                                .mirror_x = false,
                                                .mirror_y = false,
                                            }};
  disp = lvgl_port_add_disp(&disp_cfg);

  /* Rotation of the screen */
  lv_disp_set_rotation(disp, LV_DISP_ROT_NONE);

  ESP_LOGI(TAG, "Display LVGL Scroll Text");
}

void app_main() {
  gpio_set_direction(RECEIVER_LED, GPIO_MODE_OUTPUT);

  setupOled();

  ESP_LOGI(TAG, "starting up");
  spi_bus_config_t config = {
      .mosi_io_num = MOSI,
      .miso_io_num = MISO,
      .sclk_io_num = SCK,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = 0,
  };
  ESP_ERROR_CHECK(spi_bus_initialize(HSPI_HOST, &config, 1));
  spi_device_interface_config_t dev_cfg = {.clock_speed_hz = 3E6,
                                           .spics_io_num = SS,
                                           .queue_size = 16,
                                           .command_bits = 0,
                                           .address_bits = 8,
                                           .dummy_bits = 0,
                                           .mode = 0};
  spi_device_handle_t spi_device;
  ESP_ERROR_CHECK(spi_bus_add_device(HSPI_HOST, &dev_cfg, &spi_device));
  ESP_ERROR_CHECK(sx127x_create(spi_device, &device));
  ESP_ERROR_CHECK(
      sx127x_set_opmod(SX127x_MODE_SLEEP, SX127x_MODULATION_FSK, device));
  ESP_ERROR_CHECK(sx127x_set_frequency(915000000, device));
  ESP_ERROR_CHECK(
      sx127x_set_opmod(SX127x_MODE_STANDBY, SX127x_MODULATION_FSK, device));
  ESP_ERROR_CHECK(sx127x_fsk_ook_set_bitrate(4800.0, device));
  ESP_ERROR_CHECK(sx127x_fsk_set_fdev(5000.0, device));
  ESP_ERROR_CHECK(sx127x_fsk_ook_rx_set_afc_auto(true, device));
  ESP_ERROR_CHECK(sx127x_fsk_ook_rx_set_afc_bandwidth(20000.0, device));
  ESP_ERROR_CHECK(sx127x_fsk_ook_rx_set_bandwidth(5000.0, device));
  uint8_t syncWord[] = {0x12, 0xAD};
  ESP_ERROR_CHECK(sx127x_fsk_ook_set_syncword(syncWord, 2, device));
  ESP_ERROR_CHECK(
      sx127x_fsk_ook_set_address_filtering(SX127X_FILTER_NONE, 0, 0, device));
  ESP_ERROR_CHECK(sx127x_fsk_ook_set_packet_encoding(SX127X_NRZ, device));
  ESP_ERROR_CHECK(
      sx127x_fsk_ook_set_packet_format(SX127X_VARIABLE, 255, device));
  ESP_ERROR_CHECK(
      sx127x_fsk_set_data_shaping(SX127X_BT_0_5, SX127X_PA_RAMP_10, device));
  ESP_ERROR_CHECK(sx127x_fsk_ook_set_crc(SX127X_CRC_CCITT, device));
  ESP_ERROR_CHECK(
      sx127x_fsk_ook_rx_set_trigger(SX127X_RX_TRIGGER_RSSI_PREAMBLE, device));
  ESP_ERROR_CHECK(sx127x_fsk_ook_rx_set_rssi_config(SX127X_8, 0, device));
  ESP_ERROR_CHECK(
      sx127x_fsk_ook_rx_set_preamble_detector(true, 2, 0x0A, device));

  sx127x_rx_set_callback(rx_callback, device);

  BaseType_t task_code =
      xTaskCreatePinnedToCore(handle_interrupt_task, "handle interrupt", 8196,
                              device, 2, &handle_interrupt, xPortGetCoreID());
  if (task_code != pdPASS) {
    ESP_LOGE(TAG, "can't create task %d", task_code);
    sx127x_destroy(device);
    return;
  }

  gpio_install_isr_service(0);
  setup_gpio_interrupts((gpio_num_t)DIO0, device);
  setup_gpio_interrupts((gpio_num_t)DIO1, device);
  setup_gpio_interrupts((gpio_num_t)DIO2, device);

  ESP_ERROR_CHECK(
      sx127x_set_opmod(SX127x_MODE_RX_CONT, SX127x_MODULATION_FSK, device));
  while (1) {
    vTaskDelay(10000 / portTICK_PERIOD_MS);
  }
}
