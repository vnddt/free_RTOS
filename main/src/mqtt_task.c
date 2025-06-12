#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h" // Để chờ WiFi
#include "freertos/semphr.h"     // Cần cho Mutex
#include <stdio.h>
#include <string.h> // Cho strlen, sprintf
#include <stdbool.h> // << THÊM: Cho kiểu bool

#include "esp_log.h"
#include "mqtt_client.h" // Thư viện MQTT client của ESP-IDF

#include "inc/app_config.h"

// Định nghĩa cấu trúc dữ liệu cảm biến
typedef struct {
    float temperature;
    float humidity;
} sensor_data_t;

static const char *TAG = "MQTT_TASK";

// Khai báo extern cho các biến toàn cục từ main.c
extern EventGroupHandle_t wifi_event_group;
extern sensor_data_t g_display_sensor_data;
extern SemaphoreHandle_t g_display_sensor_data_mutex;

esp_mqtt_client_handle_t client = NULL;
static bool mqtt_da_ket_noi = false; // << MỚI: Biến trạng thái cho kết nối MQTT

static void log_error_if_nonzero(const char *message, int error_code) {
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    // client đã là biến toàn cục
    // esp_mqtt_client_handle_t client = event->client;
    // int msg_id;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        mqtt_da_ket_noi = true; // << MỚI: Đặt trạng thái đã kết nối
        // Bạn có thể subscribe ở đây nếu cần, ví dụ:
        // msg_id = esp_mqtt_client_subscribe(client, "/topic/qos0", 0);
        // ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        mqtt_da_ket_noi = false; // << MỚI: Xóa trạng thái đã kết nối
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        mqtt_da_ket_noi = false; // << MỚI: Xóa trạng thái đã kết nối khi có lỗi
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

static void mqtt_app_start(void) {
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URL,
        // Cân nhắc thêm keepalive nếu không phải là mặc định hoặc nếu sự cố vẫn tiếp diễn với broker
        // .session.keepalive = 60, // giây
    };

    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
    ESP_LOGI(TAG, "MQTT Client Started. URI: %s", MQTT_BROKER_URL);
}

void mqtt_task(void *pvParameters) {
    QueueHandle_t data_queue = (QueueHandle_t)pvParameters;
    sensor_data_t received_data;
    char json_payload[100];

    ESP_LOGI(TAG, "MQTT Task Started. Waiting for WiFi connection...");

    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                           WIFI_CONNECTED_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi connected. Initializing MQTT client...");
        mqtt_app_start();
    } else {
        ESP_LOGE(TAG, "WiFi connection failed. MQTT task cannot start.");
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        if (xQueueReceive(data_queue, &received_data, portMAX_DELAY) == pdPASS) {
            ESP_LOGI(TAG, "MQTT Task: Received Temp = %.1f C, Humidity = %.1f %%",
                     received_data.temperature, received_data.humidity);

            // Cập nhật biến toàn cục cho LCD, bảo vệ bằng mutex
            if (g_display_sensor_data_mutex != NULL && xSemaphoreTake(g_display_sensor_data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                g_display_sensor_data = received_data;
                xSemaphoreGive(g_display_sensor_data_mutex);
                ESP_LOGD(TAG, "Updated global display_sensor_data for LCD.");
            } else {
                ESP_LOGW(TAG, "Failed to take g_display_sensor_data_mutex in MQTT task.");
            }

            // << SỬA ĐỔI: Kiểm tra trạng thái kết nối trước khi publish
            if (client != NULL && mqtt_da_ket_noi) {
                snprintf(json_payload, sizeof(json_payload),
                         "{\"temperature\":%.1f, \"humidity\":%.1f}",
                         received_data.temperature, received_data.humidity);

                int msg_id = esp_mqtt_client_publish(client, MQTT_TOPIC, json_payload, 0, 1, 0);
                if (msg_id != -1) {
                    ESP_LOGI(TAG, "Sent publish successful (queued), msg_id=%d, data: %s", msg_id, json_payload);
                } else {
                    ESP_LOGE(TAG, "Failed to queue publish message. MQTT client might be disconnected or an error occurred.");
                }
            } else {
                if (client == NULL) {
                     ESP_LOGE(TAG, "MQTT client not initialized! Cannot publish.");
                } else { // client không NULL, nhưng mqtt_da_ket_noi là false
                     ESP_LOGW(TAG, "MQTT not connected. Skipping publish of: Temp %.1fC, Hum %.1f%%", received_data.temperature, received_data.humidity);
                }
            }
        }
    }
}