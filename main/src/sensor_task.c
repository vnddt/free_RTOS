#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include <stdio.h>
#include "esp_log.h"

// Bao gồm tệp cấu hình để lấy các định nghĩa chân GPIO và khoảng thời gian cập nhật
#include "inc/app_config.h"

// Bao gồm header từ thư viện zorxx/dht
#include "dht.h"

// Định nghĩa cấu trúc dữ liệu cảm biến (phải khớp với main.c)
typedef struct {
    float temperature;
    float humidity;
} sensor_data_t;

static const char *TAG = "SENSOR_TASK_DHT_ZORXX";

// Hàm đọc dữ liệu từ cảm biến DHT11 sử dụng thư viện zorxx/dht
static esp_err_t read_dht11_sensor_zorxx(float *temp, float *hum) {
    // Sử dụng trực tiếp DHT_TYPE_DHT11 từ thư viện dht.h
    // và DHT11_GPIO_PIN từ app_config.h
    esp_err_t result = dht_read_float_data(DHT_TYPE_DHT11, DHT11_GPIO_PIN, hum, temp);

    if (result == ESP_OK) {
        ESP_LOGD(TAG, "zorxx/dht: Đọc DHT11 OK: Temp=%.1fC, Hum=%.1f%% (GPIO %d)", *temp, *hum, DHT11_GPIO_PIN);
    } else {
        // Log lỗi chi tiết hơn
        ESP_LOGE(TAG, "zorxx/dht: Lỗi khi đọc DHT11 từ GPIO %d: %s (Mã lỗi ESP-IDF: %d)", DHT11_GPIO_PIN, esp_err_to_name(result), result);
    }
    return result;
}

void sensor_task(void *pvParameters) {
    QueueHandle_t data_queue = (QueueHandle_t)pvParameters;
    sensor_data_t current_data;

    ESP_LOGI(TAG, "Sensor Task (DHT11 - zorxx/dht) đã khởi động.");
    ESP_LOGI(TAG, "Sử dụng chân GPIO %d cho DHT11.", DHT11_GPIO_PIN); // Lấy từ app_config.h
    ESP_LOGI(TAG, "Khoảng thời gian cập nhật cảm biến: %d ms.", APP_SENSOR_UPDATE_INTERVAL_MS); // Lấy từ app_config.h

    // Kiểm tra xem chân GPIO có hợp lệ không trước khi sử dụng
    if (!GPIO_IS_VALID_GPIO(DHT11_GPIO_PIN)) {
        ESP_LOGE(TAG, "Chân GPIO %d được cấu hình cho DHT11 không hợp lệ!", DHT11_GPIO_PIN);
        ESP_LOGE(TAG, "Vui lòng kiểm tra định nghĩa DHT11_GPIO_PIN trong inc/app_config.h.");
        vTaskDelete(NULL); // Tự hủy task nếu cấu hình sai
        return;
    }

    // Thư viện zorxx/dht không yêu cầu hàm init() riêng biệt.

    while (1) {
        if (read_dht11_sensor_zorxx(&current_data.temperature, &current_data.humidity) == ESP_OK) {
            // Kiểm tra cơ bản tính hợp lệ của dữ liệu từ DHT11
            // Nhiệt độ DHT11: 0-50°C, Độ ẩm: 20-90% RH (thông thường)
            if (current_data.temperature < -10.0f || current_data.temperature > 60.0f ||
                current_data.humidity < 0.0f || current_data.humidity > 100.0f) {
                ESP_LOGW(TAG, "zorxx/dht: Dữ liệu DHT11 có vẻ không hợp lệ sau khi đọc (T=%.1f, H=%.1f). Có thể do lỗi checksum hoặc nhiễu.",
                         current_data.temperature, current_data.humidity);
                // Bạn có thể quyết định không gửi dữ liệu này hoặc gửi giá trị báo lỗi
            } else {
                 ESP_LOGI(TAG, "zorxx/dht: Cảm biến DHT11: Nhiệt độ = %.1f C, Độ ẩm = %.1f %%",
                          current_data.temperature, current_data.humidity);
            }

            // Gửi dữ liệu vào hàng đợi (queue)
            if (xQueueSend(data_queue, &current_data, pdMS_TO_TICKS(100)) != pdPASS) {
                ESP_LOGE(TAG, "Không thể gửi dữ liệu cảm biến vào hàng đợi.");
            }
        } else {
            ESP_LOGW(TAG, "zorxx/dht: Đọc dữ liệu từ cảm biến DHT11 thất bại. Sẽ thử lại sau.");
            // Lỗi đã được log chi tiết trong hàm read_dht11_sensor_zorxx()
        }
        // Đợi khoảng thời gian đã định nghĩa trong app_config.h
        vTaskDelay(pdMS_TO_TICKS(APP_SENSOR_UPDATE_INTERVAL_MS));
    }
}
