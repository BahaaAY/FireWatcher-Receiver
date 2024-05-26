#include "wifi_util.h"
#include "lora_comm.h"
#include "oled_display.h"
/* Signal Wi-Fi events on this event-group */
const int WIFI_CONNECTED_EVENT = BIT0;
static EventGroupHandle_t wifi_event_group;
static void reset_prov_long_press_cb(void *arg, void *usr_data);
/* Event handler for catching system events */
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {
  char *TAG = "EVENT_HANDLER";
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
      gpio_set_level(WIFI_NOT_PROV_PIN, 0);
      gpio_set_level(WIFI_PROV_PIN, 1);
      setup_lora();
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

static void get_device_service_name(char *service_name, size_t max) {
  uint8_t eth_mac[6];
  const char *ssid_prefix = "ESP32_";
  esp_wifi_get_mac(WIFI_IF_STA, eth_mac);
  snprintf(service_name, max, "%s%02X%02X%02X", ssid_prefix, eth_mac[3],
           eth_mac[4], eth_mac[5]);
}

static void wifi_init_sta(void) {
  /* Start Wi-Fi in station mode */
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_start());
}

void setup_wifi() {
  char *TAG = "SETUP_WIFI";
  gpio_set_level(WIFI_NOT_PROV_PIN, 1);
  gpio_set_level(WIFI_PROV_PIN, 0);
  /* Initialize Reset Wifi button */
  button_config_t gpio_btn_cfg = {
      .type = BUTTON_TYPE_GPIO,
      .long_press_time = CONFIG_BUTTON_LONG_PRESS_TIME_MS,
      .short_press_time = CONFIG_BUTTON_SHORT_PRESS_TIME_MS,
      .gpio_button_config =
          {
              .gpio_num = 0,
              .active_level = 0,
          },
  };
  button_handle_t gpio_btn = iot_button_create(&gpio_btn_cfg);
  if (NULL == gpio_btn) {
    ESP_LOGE(TAG, "Button create failed");
  }
  button_event_config_t btn_event_cfg = {
      .event = BUTTON_LONG_PRESS_START,
      .event_data.long_press.press_time = 3000,
  };

  iot_button_register_event_cb(gpio_btn, btn_event_cfg,
                               reset_prov_long_press_cb, NULL);

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
  bool reset_prov = false;
  if (reset_prov) {
    ESP_ERROR_CHECK(wifi_prov_mgr_reset_provisioning());
  }
  ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));
  /* If device is not yet provisioned start provisioning service */
  if (!provisioned) {
    ESP_LOGI(TAG, "Starting provisioning");
    gpio_set_level(WIFI_NOT_PROV_PIN, 1);
    gpio_set_level(WIFI_PROV_PIN, 0);

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
    // wifi_prov_print_qr(service_name, PROV_TRANSPORT_SOFTAP);
    display_oled_qr(service_name, PROV_TRANSPORT_SOFTAP, PROV_QR_VERSION);
  } else {
    ESP_LOGI(TAG, "Already provisioned, starting Wi-Fi STA");
    gpio_set_level(WIFI_NOT_PROV_PIN, 0);
    gpio_set_level(WIFI_PROV_PIN, 1);
    /* We don't need the manager as device is already provisioned,
     * so let's release it's resources */
    wifi_prov_mgr_deinit();
    /* Start Wi-Fi station */
    wifi_init_sta();
    gpio_set_level(WIFI_NOT_PROV_PIN, 0);
    gpio_set_level(WIFI_PROV_PIN, 1);
    setup_lora();
  }
  /* Wait for Wi-Fi connection */
  xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_EVENT, true, true,
                      portMAX_DELAY);
}

/* Handler for the optional provisioning endpoint registered by the application.
 * The data format can be chosen by applications. Here, we are using plain ascii
 * text. Applications can choose to use other formats like protobuf, JSON, XML,
 * etc.
 */
esp_err_t custom_prov_data_handler(uint32_t session_id, const uint8_t *inbuf,
                                   ssize_t inlen, uint8_t **outbuf,
                                   ssize_t *outlen, void *priv_data) {
  char *TAG = "PROV_DATA_HANDLER";
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

static void reset_prov_long_press_cb(void *arg, void *usr_data) {
  char *TAG = "BUTTON_LONG_PRESS_1_CB";
  ESP_LOGI(TAG, "BUTTON_LONG_PRESS_START_1");
  ESP_ERROR_CHECK(wifi_prov_mgr_reset_provisioning());
  ESP_LOGI(TAG, "Resetting provisioning");
  esp_restart();
}
