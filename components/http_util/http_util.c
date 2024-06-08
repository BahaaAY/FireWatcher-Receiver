#include "http_util.h"
esp_http_client_handle_t client;

char *NODE_ID = "66647bb185d75d5d5dfaddfa";
char *NODE_SECRET_KEY =
    "9f4f4f403aea369e83237080a2d8e11f7d4cff708e239f2f2f21a12c16d46554";
extern QueueHandle_t readings_queue;
void setup_http_client() {
  esp_http_client_config_t client_config = {
      .url = "http://192.168.1.3:8000/data/",
      .event_handler = NULL,
      .method = HTTP_METHOD_POST,
      .timeout_ms = 10000,
      .buffer_size = 1024,
      .transport_type = HTTP_TRANSPORT_OVER_TCP,
  };

  client = esp_http_client_init(&client_config);
  if (client == NULL) {
    ESP_LOGE("HTTP_UTIL", "Failed to initialize client");
  } else {
    ESP_LOGI("HTTP_UTIL", "Client initialized successfully");
  }
}

void send_data_to_server_task(void *pvParameters) {

  char *TAG = "HTTP_UTIL_TASK";

  setup_http_client();
  // Allocate buffer for the formatted string
  char *data = (char *)malloc(256);
  if (data == NULL) {
    fprintf(stderr, "Memory allocation failed\n");
  } else {

    for (;;) {
      SensorData data_reading;
      if (xQueueReceive(readings_queue, &data_reading, portMAX_DELAY) ==
          pdTRUE) {
        ESP_LOGI(TAG, "Received data from queue");
        // Format the string into the buffer
        snprintf(data, 256,
                 "{\"node_id\":\"%s\",\"secret_key\":\"%s\",\"humidity\":%hd,"
                 "\"temperature\":%hd,\"smoke_value\":%hd}",
                 NODE_ID, NODE_SECRET_KEY, data_reading.humidity,
                 data_reading.temperature, data_reading.smoke);

        // Print the result (for demonstration)
        printf("Formatted JSON string: %s\n", data);

        size_t data_length = strlen(data);
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_header(client, "Authorization", NODE_ID);
        esp_http_client_set_post_field(client, data, data_length);
        esp_err_t err = esp_http_client_perform(client);
        if (err == ESP_OK) {
          ESP_LOGI("HTTP_UTIL", "Data sent successfully");
        } else {
          ESP_LOGE("HTTP_UTIL", "Failed to send data");
        }
      }
    }
  }

  esp_http_client_cleanup(client);
  free(data);

  vTaskDelete(NULL);
}