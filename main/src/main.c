#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h" // Cần cho wifi_event_group
#include "freertos/semphr.h"     // Cần cho Mutex
#include <time.h>

#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"

// Include các file cấu hình và module của project
#include "inc/app_config.h"   // Chứa FIRMWARE_UPGRADE_URL, WIFI_SSID, WIFI_PASSWORD, etc.
#include "inc/ota_client.h"   // Để gọi start_ota_firmware_update
#include "inc/app_status.h"   // << QUAN TRỌNG: Chứa định nghĩa ota_status_t và các khai báo liên quan

// Định nghĩa cấu trúc dữ liệu cảm biến (đã có trong các file task)
typedef struct {
    float temperature;
    float humidity;
} sensor_data_t;


// Khai báo các TaskHandle_t để giám sát
TaskHandle_t h_wifi_task = NULL;
TaskHandle_t h_sensor_task = NULL;
TaskHandle_t h_mqtt_task = NULL;
TaskHandle_t h_ntp_task = NULL;
TaskHandle_t h_lcd_task = NULL;
TaskHandle_t h_ota_task = NULL; // Sẽ được cập nhật từ trong ota_client.c nếu cần



// Khai báo QueueHandle_t toàn cục
QueueHandle_t sensor_data_queue;

// Khai báo EventGroupHandle_t toàn cục (sẽ được tạo trong wifi_task)
extern EventGroupHandle_t wifi_event_group;

// Biến toàn cục để lưu dữ liệu cảm biến cho LCD/OLED và Mutex bảo vệ
sensor_data_t g_display_sensor_data = {0.0f, 0.0f}; // Khởi tạo giá trị ban đầu
SemaphoreHandle_t g_display_sensor_data_mutex;

// Biến toàn cục cho trạng thái OTA và Mutex bảo vệ (định nghĩa)
ota_status_t g_ota_status = OTA_STATUS_IDLE; // << QUAN TRỌNG
SemaphoreHandle_t g_ota_status_mutex;      // << QUAN TRỌNG

// Bien toan cuc luu thoi gian
struct tm g_current_timeinfo;
SemaphoreHandle_t g_current_time_mutex;
bool g_time_synchronized = false; 



// Khai báo các task từ các file khác
extern void sensor_task(void *pvParameters);
extern void wifi_task(void *pvParameters);
extern void mqtt_task(void *pvParameters);
extern void ntp_task(void *pvParameters);
extern void lcd_task(void *pvParameters); // Giữ comment nếu không dùng LCD đồng thời với OLED SSD1306 trên cùng chân I2C
// extern void oled_task(void *pvParameters); // << KHAI BÁO EXTERN CHO OLED TASK
void system_monitor_task(void *pvParameters); 


static const char *TAG_MAIN = "app_main"; // Tag riêng cho main

// Hàm chuyển đổi enum trạng thái OTA sang chuỗi (triển khai)
const char* ota_status_to_string(ota_status_t status) { // << QUAN TRỌNG
    switch (status) {
        case OTA_STATUS_IDLE: return "OTA: San sang";
        case OTA_STATUS_STARTING: return "OTA: Bat dau...";
        case OTA_STATUS_FIRMWARE_CHECK: return "OTA: Ktra FW...";
        case OTA_STATUS_DOWNLOADING: return "OTA: Dang tai...";
        case OTA_STATUS_WRITE_FLASH: return "OTA: Dang ghi...";
        case OTA_STATUS_VALIDATING: return "OTA: Xac thuc...";
        case OTA_STATUS_SUCCESS_RESTARTING: return "OTA: OK! Kdoi lai";
        case OTA_STATUS_FAILED_PARTITION: return "OTA Loi: P.vung";
        case OTA_STATUS_FAILED_HTTP_CONN: return "OTA Loi: HTTP";
        case OTA_STATUS_FAILED_BEGIN: return "OTA Loi: Begin";
        case OTA_STATUS_FAILED_READ: return "OTA Loi: Doc";
        case OTA_STATUS_FAILED_WRITE: return "OTA Loi: Ghi";
        case OTA_STATUS_FAILED_END_VALIDATE: return "OTA Loi: Validate";
        case OTA_STATUS_FAILED_SET_BOOT: return "OTA Loi: SetBoot";
        case OTA_STATUS_NO_UPDATE_AVAILABLE: return "OTA: K co C.Nhat";
        default: return "OTA: T.thai K ro";
    }
}

void app_main() {
    // Khởi tạo NVS (cần cho WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Khởi tạo TCP/IP adapter và vòng lặp sự kiện mặc định (cần cho WiFi)
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Tạo Mutex để bảo vệ dữ liệu hiển thị cảm biến
    g_display_sensor_data_mutex = xSemaphoreCreateMutex();
    if (g_display_sensor_data_mutex == NULL) {
        ESP_LOGE(TAG_MAIN, "Failed to create g_display_sensor_data_mutex. Halting.");
        while(1); // Dừng ở đây nếu không tạo được mutex
    }

    // Tạo Mutex để bảo vệ thời gian hiện tại
    g_current_time_mutex = xSemaphoreCreateMutex();
    if (g_current_time_mutex == NULL) {
        ESP_LOGE(TAG_MAIN, "Failed to create g_current_time_mutex. Halting.");
        while(1);
    }

    // Tạo Mutex để bảo vệ trạng thái OTA
    g_ota_status_mutex = xSemaphoreCreateMutex(); // << QUAN TRỌNG
    if (g_ota_status_mutex == NULL) {
        ESP_LOGE(TAG_MAIN, "Failed to create g_ota_status_mutex. Halting.");
        while(1);
    }

    // Tạo hàng đợi dữ liệu cảm biến
    sensor_data_queue = xQueueCreate(SENSOR_DATA_QUEUE_SIZE, sizeof(sensor_data_t));

    if (sensor_data_queue == NULL) {
        ESP_LOGE(TAG_MAIN, "Failed to create sensor_data_queue.");
        return;
    }

    // Tạo wifi_task trước tiên để nó có thể tạo wifi_event_group và bắt đầu kết nối
    xTaskCreate(wifi_task, "WiFi_Task", 4096, NULL, 6, &h_wifi_task); // WiFi task với độ ưu tiên cao

    ESP_LOGI(TAG_MAIN, "Waiting for WiFi connection to be established by wifi_task...");

    int wait_retries = 0;
    // Đợi wifi_event_group được tạo bởi wifi_task
    while (wifi_event_group == NULL && wait_retries < 10) { // Tăng số lần thử hoặc thời gian chờ nếu cần
        ESP_LOGW(TAG_MAIN, "Waiting for wifi_event_group to be created by wifi_task... (%d/10)", wait_retries + 1);
        vTaskDelay(pdMS_TO_TICKS(500));
        wait_retries++;
    }

    if (wifi_event_group == NULL) {
        ESP_LOGE(TAG_MAIN, "wifi_event_group was not created by wifi_task. Aborting network-dependent tasks.");
    } else {
        ESP_LOGI(TAG_MAIN, "wifi_event_group detected. Waiting for WIFI_CONNECTED_BIT...");
        EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                            WIFI_CONNECTED_BIT,
                                            pdFALSE,
                                            pdFALSE,
                                            portMAX_DELAY);

        if (bits & WIFI_CONNECTED_BIT) {
            ESP_LOGI(TAG_MAIN, "WiFi Connected. Starting application tasks and OTA process.");

            xTaskCreate(sensor_task, "Sensor_Task", 2048, (void*)sensor_data_queue, 5, &h_sensor_task);
            xTaskCreate(mqtt_task, "MQTT_Task", 4096, (void*)sensor_data_queue, 4, &h_mqtt_task);
            xTaskCreate(ntp_task, "NTP_Task", 3072, NULL, 3, &h_ntp_task);
            xTaskCreate(lcd_task, "LCD_Task", 2560, NULL, 4, &h_lcd_task); 
            // ESP_LOGI(TAG_MAIN, "Attempting to start OTA firmware update...");
            // start_ota_firmware_update(FIRMWARE_UPGRADE_URL);
            xTaskCreate(system_monitor_task, "Monitor_Task", 3072, NULL, 1, NULL);

        } else {
            ESP_LOGE(TAG_MAIN, "WiFi connection failed (event bit not set). Cannot start network tasks or OTA.");
        }
    }
    ESP_LOGI(TAG_MAIN, "app_main finished its setup. Tasks are running.");
}



// Tác vụ giám sát tài nguyên hệ thống
void system_monitor_task(void *pvParameters) {
    ESP_LOGI("MONITOR_TASK", "Bắt đầu giám sát hệ thống...");

    while(1) {
        printf("\n\n===================== SYSTEM STATUS =====================\n");

        // 1. Giám sát HEAP
        printf("Free Heap Size: %lu bytes\n", esp_get_free_heap_size());

        // 2. Giám sát STACK của các Task
        printf("Task Stack High Water Mark (bytes còn lại):\n");
        if(h_wifi_task) printf("- WiFi_Task: %d\n", uxTaskGetStackHighWaterMark(h_wifi_task) * sizeof(StackType_t));
        if(h_sensor_task) printf("- Sensor_Task: %d\n", uxTaskGetStackHighWaterMark(h_sensor_task) * sizeof(StackType_t));
        if(h_mqtt_task) printf("- MQTT_Task: %d\n", uxTaskGetStackHighWaterMark(h_mqtt_task) * sizeof(StackType_t));
        if(h_ntp_task) printf("- NTP_Task: %d\n", uxTaskGetStackHighWaterMark(h_ntp_task) * sizeof(StackType_t));
        if(h_lcd_task) printf("- LCD_Task: %d\n", uxTaskGetStackHighWaterMark(h_lcd_task) * sizeof(StackType_t));
        // Lưu ý: ota_task chỉ chạy khi có cập nhật, bạn cần theo dõi riêng khi test OTA

        char stats_buffer[1024];
        vTaskGetRunTimeStats(stats_buffer);
        printf("\nTask CPU Usage:\n%s\n", stats_buffer);

        printf("=========================================================\n\n");

        vTaskDelay(pdMS_TO_TICKS(10000)); // 
    }
}
