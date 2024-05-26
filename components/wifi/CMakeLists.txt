idf_component_register(SRCS "wifi_util.c"
                       INCLUDE_DIRS "." REQUIRES freertos driver wifi_provisioning nvs_flash esp_wifi oled lora_comm) 