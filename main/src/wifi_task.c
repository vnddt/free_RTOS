#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <stdio.h>
#include <string.h>

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"

#include "inc/app_config.h"

static const char *TAG = "WIFI_MANAGER_TASK";

// EventGroupHandle_t được khai báo toàn cục để các task khác có thể truy cập
EventGroupHandle_t wifi_event_group;

// *** THAY ĐỔI 1: Không cần biến đếm số lần retry nữa ***
// static int s_retry_num = 0;
// #define WIFI_MAXIMUM_RETRY  5 

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "Che do Station da khoi dong, bat dau ket noi WiFi...");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        // *** THAY ĐỔI 2: Luôn luôn kết nối lại, không bao giờ bỏ cuộc ***
        ESP_LOGW(TAG, "Mat ket noi WiFi. Se thu ket noi lai sau 5 giay...");
        vTaskDelay(pdMS_TO_TICKS(5000)); // Thêm độ trễ nhỏ để tránh spam kết nối
        esp_wifi_connect();
        ESP_LOGI(TAG, "Dang thu ket noi lai den AP...");
        // Không set cờ WIFI_FAIL_BIT nữa vì chúng ta luôn thử lại.
        // Chỉ xóa cờ WIFI_CONNECTED_BIT để các task khác biết là đang offline.
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);

    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Da ket noi WiFi va nhan duoc IP:" IPSTR, IP2STR(&event->ip_info.ip));
        // Đã kết nối thành công, set cờ WIFI_CONNECTED_BIT
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void) {
    wifi_event_group = xEventGroupCreate();

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta hoan tat.");
}

void wifi_task(void *pvParameters) {
    ESP_LOGI(TAG, "Task Quan Ly Ket Noi da bat dau.");
    wifi_init_sta(); // Khởi tạo và bắt đầu kết nối WiFi

    // *** THAY ĐỔI 3: Đơn giản hóa vòng lặp của task ***
    // Sau khi khởi tạo, toàn bộ logic đã nằm trong event_handler.
    // Task này chỉ cần tồn tại để handler hoạt động.
    // Chúng ta không cần làm gì trong vòng lặp while nữa.
    while(1) {
        // Chỉ cần delay để nhường CPU cho các task khác.
        vTaskDelay(portMAX_DELAY);
    }
}