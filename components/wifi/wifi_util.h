#ifndef WIFI_UTIL_H
#define WIFI_UTIL_H

#include <esp_err.h>
#include <esp_log.h>

#include "esp_mac.h"
#include "esp_wifi.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "nvs_flash.h"
#include <wifi_provisioning/scheme_softap.h>

#include <driver/gpio.h>

#define PROV_QR_VERSION "v1"
#define PROV_TRANSPORT_SOFTAP "softap"
#define PROV_TRANSPORT_BLE "ble"
#define QRCODE_BASE_URL "https://espressif.github.io/esp-jumpstart/qrcode.html"

#define WIFI_NOT_PROV_PIN 33
#define WIFI_PROV_PIN 32

void setup_wifi();

#endif