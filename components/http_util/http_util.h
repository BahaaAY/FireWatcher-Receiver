#ifndef HTTP_UTIL_H
#define HTTP_UTIL_H

#include "esp_http_client.h"
#include "esp_log.h"

void setup_http_client();

void send_data_to_server(int16_t *temperature, int16_t *humidity,
                         int16_t *smoke);

#endif