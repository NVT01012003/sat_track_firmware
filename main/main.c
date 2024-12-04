// C libraries and idf libraries
#include "esp_event.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include <FreeRTOS/FreeRTOS.h>
#include <FreeRTOS/queue.h>
#include <FreeRTOS/task.h>
#include <math.h>
#include <stdio.h>

// Customs libraries
#include "euler_angles.h"
#include "gps.h"
#include "i2c_init.h"
#include "mpu9250.h"
#include "qmc5883l.h"
#include "step.h"

// #include "kalman.h"

static const char *TAG = "MAIN";
static const char *WIFI_TAG = "WIFI";
#define WIFI_SSID "NVT"      // Wifi ssid
#define WIFI_PASS "12345678" // Wifi password

const int WIFI_CONNECTED_BIT = BIT0;
static EventGroupHandle_t wifi_event_group;

// Task function
void Get_data_task(void *pvParameters);
void Wifi_connection_task(void *pvParameters);

// Wifi conect function
void wifi_event_handler(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data);
void wifi_init_sta();

void Test_task(void *pvParameters) {
  while (1) {
    ESP_LOGI(TAG, "Test task is running!");
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// App main
void app_main(void) {

  xTaskCreate(Wifi_connection_task, "Wifi_station_init", 4096, NULL, 10, NULL);
  xTaskCreate(Test_task, "Test_task", 4096, NULL, 2, NULL);
  // xTaskCreate(Get_data_task, "Sensor_read_data", 4096, NULL, 2, NULL);
}

// Get sensors data
void Get_data_task(void *pvParameters) {

  esp_err_t ret = i2c_master_init();
  if (ret != ESP_OK) {
    ESP_LOGE(I2C_TAG, "Failed to initialize I2C: %s", esp_err_to_name(ret));
    return;
  }
  // qmc5883l init
  qmc5883l_init();
  ESP_LOGI(QMC_TAG, "QMC5883L initialized");
  Sensor_data accel = {};
  Sensor_data gyro = {};

  Euler_angles angles;
  int16_t x, y, z;
  while (1) {
    // Read data
    mpu9250_read_sensor_data(&accel, &gyro);
    qmc5883l_read_magnetometer(&x, &y, &z);
    // Handle data
    angles = Calc_Roll_Pitch_Yaw(&accel, &(Sensor_data){x, y, z});
    ESP_LOGI(CALC_TAG, "Eulers angels roll: %f, pitch: %f, yaw: %f",
             angles.roll, angles.pitch, angles.yaw);
    Angles result = calc_sat_angles(angles);
    ESP_LOGI(CALC_TAG, ": Elevation: %f, Azimuth: %f", result.elevation,
             result.azimuth);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

void Wifi_connection_task(void *pvParameters) {
  // Initialize nvs
  ESP_ERROR_CHECK(nvs_flash_init());

  ESP_LOGI(WIFI_TAG, "Wifi initializing...");
  wifi_init_sta();

  // Waiting for connection
  xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE,
                      portMAX_DELAY);

  ESP_LOGI(WIFI_TAG, "ESP32 is already!");
  bool shouldStop = false;

  while (!shouldStop) {
    vTaskDelay(pdMS_TO_TICKS(1000)); // Delay 1 second
  }

  // Delete itself
  vTaskDelete(NULL);
}

void wifi_event_handler(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    ESP_LOGI(WIFI_TAG, "Wifi disconected, reconnecting...");
    esp_wifi_connect();
    xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(WIFI_TAG, "Connected, IP address: " IPSTR,
             IP2STR(&event->ip_info.ip));
    xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
  }
}

void task(void) {}

void wifi_init_sta() {
  wifi_event_group = xEventGroupCreate();

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  esp_event_handler_instance_t instance_any_id;
  esp_event_handler_instance_t instance_got_ip;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL,
      &instance_any_id));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL,
      &instance_got_ip));

  wifi_config_t wifi_config = {
      .sta =
          {
              .ssid = WIFI_SSID,
              .password = WIFI_PASS,
              .threshold.authmode = WIFI_AUTH_WPA2_PSK,
          },
  };

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(WIFI_TAG, "WiFi STA Initialized.");
}