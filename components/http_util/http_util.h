#ifndef HTTP_UTIL_H
#define HTTP_UTIL_H

#include "esp_http_client.h"
#include "esp_log.h"
#include "sensor_data.h"

void setup_http_client();

void send_data_to_server_task(void *pvParameters);

#endif