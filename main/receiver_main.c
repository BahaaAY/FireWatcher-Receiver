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

#include "esp_mac.h"
#include "esp_wifi.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "nvs_flash.h"

#include "qrcode.h"
#include <wifi_provisioning/scheme_softap.h>

#include "./espressif__qrcode/qrcodegen.h"

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

int16_t humidity, temperature, rawSmoke, calSmokeVoltage = 0;

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
  TAG = "RX_CALLBACK";
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
}

void setupLora() {
  TAG = "SETUP_LORA";
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
}

/* Signal Wi-Fi events on this event-group */
const int WIFI_CONNECTED_EVENT = BIT0;
static EventGroupHandle_t wifi_event_group;

#define PROV_QR_VERSION "v1"
#define PROV_TRANSPORT_SOFTAP "softap"
#define PROV_TRANSPORT_BLE "ble"
#define QRCODE_BASE_URL "https://espressif.github.io/esp-jumpstart/qrcode.html"

/* Event handler for catching system events */
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {
#ifdef CONFIG_EXAMPLE_RESET_PROV_MGR_ON_FAILURE
  static int retries;
#endif
  if (event_base == WIFI_PROV_EVENT) {
    switch (event_id) {
    case WIFI_PROV_START:
      ESP_LOGI(TAG, "Provisioning started");
      break;
    case WIFI_PROV_CRED_RECV: {
      wifi_sta_config_t *wifi_sta_cfg = (wifi_sta_config_t *)event_data;
      ESP_LOGI(TAG,
               "Received Wi-Fi credentials"
               "\n\tSSID     : %s\n\tPassword : %s",
               (const char *)wifi_sta_cfg->ssid,
               (const char *)wifi_sta_cfg->password);
      break;
    }
    case WIFI_PROV_CRED_FAIL: {
      wifi_prov_sta_fail_reason_t *reason =
          (wifi_prov_sta_fail_reason_t *)event_data;
      ESP_LOGE(TAG,
               "Provisioning failed!\n\tReason : %s"
               "\n\tPlease reset to factory and retry provisioning",
               (*reason == WIFI_PROV_STA_AUTH_ERROR)
                   ? "Wi-Fi station authentication failed"
                   : "Wi-Fi access-point not found");
#ifdef CONFIG_EXAMPLE_RESET_PROV_MGR_ON_FAILURE
      retries++;
      if (retries >= CONFIG_EXAMPLE_PROV_MGR_MAX_RETRY_CNT) {
        ESP_LOGI(TAG, "Failed to connect with provisioned AP, reseting "
                      "provisioned credentials");
        wifi_prov_mgr_reset_sm_state_on_failure();
        retries = 0;
      }
#endif
      break;
    }
    case WIFI_PROV_CRED_SUCCESS:
      ESP_LOGI(TAG, "Provisioning successful");
#ifdef CONFIG_EXAMPLE_RESET_PROV_MGR_ON_FAILURE
      retries = 0;
#endif
      break;
    case WIFI_PROV_END:
      /* De-initialize manager once provisioning is finished */
      wifi_prov_mgr_deinit();
      break;
    default:
      break;
    }
  } else if (event_base == WIFI_EVENT) {
    switch (event_id) {
    case WIFI_EVENT_STA_START:
      esp_wifi_connect();
      break;
    case WIFI_EVENT_STA_DISCONNECTED:
      ESP_LOGI(TAG, "Disconnected. Connecting to the AP again...");
      esp_wifi_connect();
      break;
#ifdef CONFIG_EXAMPLE_PROV_TRANSPORT_SOFTAP
    case WIFI_EVENT_AP_STACONNECTED:
      ESP_LOGI(TAG, "SoftAP transport: Connected!");
      break;
    case WIFI_EVENT_AP_STADISCONNECTED:
      ESP_LOGI(TAG, "SoftAP transport: Disconnected!");
      break;
#endif
    default:
      break;
    }
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "Connected with IP Address:" IPSTR,
             IP2STR(&event->ip_info.ip));
    /* Signal main application to continue execution */
    xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_EVENT);

  } else if (event_base == PROTOCOMM_SECURITY_SESSION_EVENT) {
    switch (event_id) {
    case PROTOCOMM_SECURITY_SESSION_SETUP_OK:
      ESP_LOGI(TAG, "Secured session established!");
      break;
    case PROTOCOMM_SECURITY_SESSION_INVALID_SECURITY_PARAMS:
      ESP_LOGE(TAG, "Received invalid security parameters for establishing "
                    "secure session!");
      break;
    case PROTOCOMM_SECURITY_SESSION_CREDENTIALS_MISMATCH:
      ESP_LOGE(TAG, "Received incorrect username and/or PoP for establishing "
                    "secure session!");
      break;
    default:
      break;
    }
  }
}

static void wifi_init_sta(void) {
  /* Start Wi-Fi in station mode */
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_start());
}

static void get_device_service_name(char *service_name, size_t max) {
  uint8_t eth_mac[6];
  const char *ssid_prefix = "ESP32_";
  esp_wifi_get_mac(WIFI_IF_STA, eth_mac);
  snprintf(service_name, max, "%s%02X%02X%02X", ssid_prefix, eth_mac[3],
           eth_mac[4], eth_mac[5]);
}

/* Handler for the optional provisioning endpoint registered by the application.
 * The data format can be chosen by applications. Here, we are using plain ascii
 * text. Applications can choose to use other formats like protobuf, JSON, XML,
 * etc.
 */
esp_err_t custom_prov_data_handler(uint32_t session_id, const uint8_t *inbuf,
                                   ssize_t inlen, uint8_t **outbuf,
                                   ssize_t *outlen, void *priv_data) {
  if (inbuf) {
    ESP_LOGI(TAG, "Received data: %.*s", inlen, (char *)inbuf);
  }
  char response[] = "SUCCESS";
  *outbuf = (uint8_t *)strdup(response);
  if (*outbuf == NULL) {
    ESP_LOGE(TAG, "System out of memory");
    return ESP_ERR_NO_MEM;
  }
  *outlen = strlen(response) + 1; /* +1 for NULL terminating byte */

  return ESP_OK;
}

#define LV_COLOR_WHITE lv_color_make(255, 255, 255)
#define LV_COLOR_BLACK lv_color_make(0, 0, 0)
#define QR_CODE_SIZE 33
void display_oled_qr() {
  TAG = "QRCODE_DISPLAY";
  ESP_LOGI(TAG, "Displaying QR code on OLED");
  lv_obj_t *qrcode_obj =
      lv_qrcode_create(lv_scr_act(), 60, LV_COLOR_BLACK,
                       LV_COLOR_WHITE); // Replace values as needed

  // Update the QR code content
  const char *link = "your_link_here";
  lv_qrcode_update(qrcode_obj, link, strlen(link));
  lv_obj_align(qrcode_obj, LV_ALIGN_CENTER, 0, 0);
  lv_scr_load(lv_scr_act());
}

static void wifi_prov_print_qr(const char *name, const char *transport) {
  if (!name || !transport) {
    ESP_LOGW(TAG, "Cannot generate QR code payload. Data missing.");
    return;
  }
  char payload[150] = {0};
  snprintf(payload, sizeof(payload),
           "{\"ver\":\"%s\",\"name\":\"%s\""
           ",\"transport\":\"%s\"}",
           PROV_QR_VERSION, name, transport);
  ESP_LOGI(
      TAG,
      "Scan this QR code from the provisioning application for Provisioning.");
  ESP_LOGI("mY kao", "QR Code Payload: %s", payload);
  esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
  esp_qrcode_generate(&cfg, payload);
  ESP_LOGI(TAG,
           "If QR code is not visible, copy paste the below URL in a "
           "browser.\n%s?data=%s",
           QRCODE_BASE_URL, payload);
}

void setupWifi() {
  TAG = "SETUP_WIFI";
  /* Initialize NVS partition */
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    /* NVS partition was truncated
     * and needs to be erased */
    ESP_ERROR_CHECK(nvs_flash_erase());

    /* Retry nvs_flash_init */
    ESP_ERROR_CHECK(nvs_flash_init());
  }

  /* Initialize TCP/IP */
  ESP_ERROR_CHECK(esp_netif_init());

  /* Initialize the event loop */
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  wifi_event_group = xEventGroupCreate();

  /* Register our event handler for Wi-Fi, IP and Provisioning related events */
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID,
                                             &event_handler, NULL));

  ESP_ERROR_CHECK(esp_event_handler_register(PROTOCOMM_SECURITY_SESSION_EVENT,
                                             ESP_EVENT_ANY_ID, &event_handler,
                                             NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                             &event_handler, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                             &event_handler, NULL));

  /* Initialize Wi-Fi including netif with default config */
  esp_netif_create_default_wifi_sta();
  esp_netif_create_default_wifi_ap();
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  /* Configuration for the provisioning manager */
  wifi_prov_mgr_config_t config = {.scheme = wifi_prov_scheme_softap,
                                   .scheme_event_handler =
                                       WIFI_PROV_EVENT_HANDLER_NONE};
  /* Initialize provisioning manager with the
   * configuration parameters set above */
  ESP_ERROR_CHECK(wifi_prov_mgr_init(config));
  bool provisioned = false;
  bool reset_prov = true;
  if (reset_prov) {
    ESP_ERROR_CHECK(wifi_prov_mgr_reset_provisioning());
  }
  ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));
  /* If device is not yet provisioned start provisioning service */
  if (!provisioned) {
    ESP_LOGI(TAG, "Starting provisioning");

    /* What is the Device Service Name that we want
     * This translates to :
     *     - Wi-Fi SSID when scheme is wifi_prov_scheme_softap
     *     - device name when scheme is wifi_prov_scheme_ble
     */
    char service_name[12];
    get_device_service_name(service_name, sizeof(service_name));

    wifi_prov_security_t security = WIFI_PROV_SECURITY_0;

    /* What is the service key (could be NULL)
     * This translates to :
     *     - Wi-Fi password when scheme is wifi_prov_scheme_softap
     *          (Minimum expected length: 8, maximum 64 for WPA2-PSK)
     *     - simply ignored when scheme is wifi_prov_scheme_ble
     */
    const char *service_key = NULL;

    /* Start provisioning service */
    ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(
        security, (const void *)NULL, service_name, service_key));

    /* PRINT QR */
    wifi_prov_print_qr(service_name, PROV_TRANSPORT_SOFTAP);

  } else {
    ESP_LOGI(TAG, "Already provisioned, starting Wi-Fi STA");

    /* We don't need the manager as device is already provisioned,
     * so let's release it's resources */
    wifi_prov_mgr_deinit();
    /* Start Wi-Fi station */
    wifi_init_sta();
  }
  /* Wait for Wi-Fi connection */
  xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_EVENT, true, true,
                      portMAX_DELAY);
}
void app_main() {
  ESP_LOGI(TAG, "starting up");
  // gpio_set_direction(RECEIVER_LED, GPIO_MODE_OUTPUT);
  setupOled();
  // setupLora();
  // gpio_set_level(RECEIVER_LED, 0);
  // setupWifi();
  display_oled_qr();

  while (1) {
    vTaskDelay(10000 / portTICK_PERIOD_MS);
  }
}
