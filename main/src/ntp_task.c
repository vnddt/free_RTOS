#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_sntp.h"

#include "inc/app_config.h"

static const char *TAG = "NTP_TASK";

// Khai báo extern cho các tài nguyên toàn cục
extern EventGroupHandle_t wifi_event_group;
extern struct tm g_current_timeinfo;
extern SemaphoreHandle_t g_current_time_mutex;
extern bool g_time_synchronized;

// Callback được gọi khi thời gian đồng bộ thành công
void time_sync_notification_cb(struct timeval *tv) {
    ESP_LOGI(TAG, "Thoi gian da dong bo: %s", ctime(&tv->tv_sec));
    g_time_synchronized = true; // >> SỬA: Dùng cờ toàn cục
}

static void initialize_sntp(void) {
    ESP_LOGI(TAG, "Initializing SNTP");
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_init();
}

static void obtain_time(void) {
    ESP_LOGI(TAG, "Dang cho WiFi ket noi de dong bo NTP...");
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    ESP_LOGI(TAG, "WiFi da ket noi. Bat dau khoi tao NTP.");

    initialize_sntp();

    int retry = 0;
    const int retry_count = 15;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
        ESP_LOGI(TAG, "Dang cho dong bo thoi gian... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }

    if (retry == retry_count || !g_time_synchronized) { // >> SỬA: Dùng cờ toàn cục
        ESP_LOGE(TAG, "Dong bo thoi gian voi server NTP that bai.");
        return;
    }
    ESP_LOGI(TAG, "Dong bo thoi gian thanh cong.");
}

void ntp_task(void *pvParameters) {
    obtain_time();

    time_t now;
    struct tm timeinfo;
    char strftime_buf[64];

    setenv("TZ", "ICT-7", 1);
    tzset();

    while (1) {
        if (g_time_synchronized) {
            time(&now);
            localtime_r(&now, &timeinfo);

            // >> MỚI: Cập nhật thời gian vào biến toàn cục, bảo vệ bằng mutex
            if (xSemaphoreTake(g_current_time_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                g_current_timeinfo = timeinfo; // Sao chép thời gian
                xSemaphoreGive(g_current_time_mutex);
            }
            
            strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
            ESP_LOGI(TAG, "Thoi gian hien tai: %s", strftime_buf);
        } else {
            ESP_LOGW(TAG, "Thoi gian chua duoc dong bo.");
        }
        
        // Cập nhật thời gian mỗi 30 giây
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}