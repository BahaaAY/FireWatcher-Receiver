#include "lora_comm.h"
#include "enc_utils.h"
#define AES_KEY_SIZE 32 // 256 bits
#define AES_IV_SIZE 12  // 96 bits
#define AES_TAG_SIZE 16
extern sx127x *device;
extern int16_t humidity, temperature, rawSmoke, calSmokeVoltage;
extern TaskHandle_t handle_interrupt;
extern int total_packets_received;
void setup_lora() {
  char *TAG = "SETUP_LORA";
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
  setup_gpio_interrupts((gpio_num_t)DIO0);
  setup_gpio_interrupts((gpio_num_t)DIO1);
  setup_gpio_interrupts((gpio_num_t)DIO2);

  ESP_ERROR_CHECK(
      sx127x_set_opmod(SX127x_MODE_RX_CONT, SX127x_MODULATION_FSK, device));
}

void rx_callback(sx127x *device, uint8_t *data, uint16_t data_length) {
  char *TAG = "RX_CALLBACK";
  gpio_set_level(RECEIVER_LED, 1);

  uint8_t payload[514];
  const char SYMBOLS[] = "0123456789ABCDEF";
  for (size_t i = 0; i < data_length; i++) {
    uint8_t cur = data[i];
    payload[2 * i] = SYMBOLS[cur >> 4];
    payload[2 * i + 1] = SYMBOLS[cur & 0x0F];
  }
  payload[data_length * 2] = '\0';

  uint8_t decrypted_data[128]; // Adjust size accordingly
  int ret;

  // Decrypt the received data
  ret = decrypt_data(data, data_length, decrypted_data);
  if (ret != 0) {
    ESP_LOGE(TAG, "Failed to decrypt data");
    return;
  }

  // Print decrypted data for debugging
  print_hex("Decrypted Data", decrypted_data,
            data_length - AES_IV_SIZE - AES_TAG_SIZE);

  int32_t frequency_error;
  ESP_ERROR_CHECK(sx127x_rx_get_frequency_error(device, &frequency_error));
  ESP_LOGI(TAG, "received: %d %s freq_error: %" PRId32, data_length, payload,
           frequency_error);
  // print rssi
  int16_t rssi;
  ESP_ERROR_CHECK(sx127x_rx_get_packet_rssi(device, &rssi));
  ESP_LOGI(TAG, "with rssi: %d", rssi);

  unpackData(decrypted_data, &humidity, &temperature, &rawSmoke,
             &calSmokeVoltage);
  ESP_LOGI(TAG, "Humidity: %d%%", humidity);
  ESP_LOGI(TAG, "Temperature: %dC", temperature);
  ESP_LOGI(TAG, "Raw Smoke: %d", rawSmoke);
  ESP_LOGI(TAG, "Calibrated Smoke: %d", calSmokeVoltage);

  ESP_LOGI(TAG, "total packets received: %d", total_packets_received);

  total_packets_received++;
  display_oled(&temperature, &humidity, &rawSmoke, &calSmokeVoltage);
  send_data_to_server(&temperature, &humidity, &rawSmoke);
  gpio_set_level(RECEIVER_LED, 0);
  ESP_LOGI(TAG, "done");
}

void IRAM_ATTR handle_interrupt_fromisr(void *arg) {
  xTaskResumeFromISR(handle_interrupt);
}

void setup_gpio_interrupts(gpio_num_t gpio) {
  gpio_set_direction(gpio, GPIO_MODE_INPUT);
  gpio_pulldown_en(gpio);
  gpio_pullup_dis(gpio);
  gpio_set_intr_type(gpio, GPIO_INTR_POSEDGE);
  gpio_isr_handler_add(gpio, handle_interrupt_fromisr, (void *)device);
}

void handle_interrupt_task(void *arg) {
  while (1) {
    vTaskSuspend(NULL);
    sx127x_handle_interrupt((sx127x *)arg);
  }
}

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