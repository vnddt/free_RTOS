#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_err.h"

#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_flash_partitions.h"
#include "esp_partition.h"

// Include các header của project
#include "inc/app_config.h" // Để sử dụng OTA_BUFF_SIZE
#include "inc/ota_client.h"
#include "inc/app_status.h" // Chứa định nghĩa ota_status_t và khai báo extern

static const char *TAG = "ota_client"; // Tag riêng cho file này

// Buffer để đọc dữ liệu từ HTTP response, sử dụng OTA_BUFF_SIZE từ app_config.h
static char ota_write_data[OTA_BUFF_SIZE + 1] = {0};

// Hàm helper để cập nhật trạng thái OTA một cách an toàn
static void set_ota_status(ota_status_t new_status) {
    if (g_ota_status_mutex != NULL && xSemaphoreTake(g_ota_status_mutex, portMAX_DELAY) == pdTRUE) {
        g_ota_status = new_status;
        xSemaphoreGive(g_ota_status_mutex);
    } else {
        ESP_LOGE(TAG, "Failed to take OTA status mutex in ota_client!");
    }
}

// Hàm xử lý sự kiện HTTP (static, chỉ dùng nội bộ trong file này)
static esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
        break;
    case HTTP_EVENT_REDIRECT:
        ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
        esp_http_client_set_header(evt->client, "Host", ""); // Xóa Host header để chuyển hướng
        break;
    }
    return ESP_OK;
}

// Task thực hiện OTA
static void ota_task(void *pvParameter) {
    char *firmware_url = (char *)pvParameter;
    ESP_LOGI(TAG, "Starting OTA task with URL: %s", firmware_url);
    set_ota_status(OTA_STATUS_STARTING);

    esp_err_t err;
    esp_ota_handle_t update_handle = 0;
    const esp_partition_t *update_partition = NULL;

    ESP_LOGI(TAG, "Looking for next OTA update partition...");
    const esp_partition_t *configured = esp_ota_get_boot_partition();
    update_partition = esp_ota_get_next_update_partition(configured);

    if (update_partition == NULL) {
        ESP_LOGE(TAG, "Valid OTA partition not found.");
        set_ota_status(OTA_STATUS_FAILED_PARTITION);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Found partition: type %d, subtype %d, offset 0x%x, size 0x%x",
             update_partition->type, update_partition->subtype, update_partition->address, update_partition->size);

    esp_http_client_config_t config = {
        .url = firmware_url,
        .event_handler = _http_event_handler,
        .keep_alive_enable = true,
        .timeout_ms = 15000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        set_ota_status(OTA_STATUS_FAILED_HTTP_CONN);
        vTaskDelete(NULL);
        return;
    }

    err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        set_ota_status(OTA_STATUS_FAILED_HTTP_CONN);
        esp_http_client_cleanup(client);
        vTaskDelete(NULL);
        return;
    }

    int content_length = esp_http_client_fetch_headers(client);
    if (content_length < 0 && esp_http_client_get_status_code(client) != 200) {
        ESP_LOGE(TAG, "HTTP client fetch headers failed, status code: %d", esp_http_client_get_status_code(client));
        set_ota_status(OTA_STATUS_FAILED_HTTP_CONN);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Estimated firmware size (Content-Length): %d bytes", content_length);
    set_ota_status(OTA_STATUS_DOWNLOADING);

    err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
        set_ota_status(OTA_STATUS_FAILED_BEGIN);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "esp_ota_begin succeeded. Writing firmware...");

    int binary_file_length = 0;
    int data_read = 0;
    bool writing_started = false;

    while (1) {
        data_read = esp_http_client_read(client, ota_write_data, OTA_BUFF_SIZE);
        if (!writing_started && data_read > 0) {
            set_ota_status(OTA_STATUS_WRITE_FLASH);
            writing_started = true;
        }

        if (data_read < 0) {
            ESP_LOGE(TAG, "Error reading HTTP data, errno from http client: %d", esp_http_client_get_errno(client));
            set_ota_status(OTA_STATUS_FAILED_READ);
            esp_ota_abort(update_handle);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            vTaskDelete(NULL);
            return;
        } else if (data_read > 0) {
            err = esp_ota_write(update_handle, (const void *)ota_write_data, data_read);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Error writing OTA data: %s", esp_err_to_name(err));
                set_ota_status(OTA_STATUS_FAILED_WRITE);
                esp_ota_abort(update_handle);
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                vTaskDelete(NULL);
                return;
            }
            binary_file_length += data_read;
            ESP_LOGD(TAG, "Written %d bytes, total %d bytes", data_read, binary_file_length);
        } else if (data_read == 0) {
            if (esp_http_client_is_complete_data_received(client)) {
                 ESP_LOGI(TAG, "Download complete. Total size: %d bytes", binary_file_length);
                 break;
            } else {
                ESP_LOGW(TAG, "data_read is 0, but stream is not complete. Retrying shortly.");
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        }
    }

    set_ota_status(OTA_STATUS_VALIDATING);
    err = esp_ota_end(update_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            ESP_LOGE(TAG, "Image validation failed! Firmware may be corrupt or improperly signed.");
        } else {
            ESP_LOGE(TAG, "esp_ota_end failed (%s)!", esp_err_to_name(err));
        }
        set_ota_status(OTA_STATUS_FAILED_END_VALIDATE);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        vTaskDelete(NULL);
        return;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed (%s)!", esp_err_to_name(err));
        set_ota_status(OTA_STATUS_FAILED_SET_BOOT);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "OTA update successful! Preparing to restart into new firmware.");
    set_ota_status(OTA_STATUS_SUCCESS_RESTARTING);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    vTaskDelay(pdMS_TO_TICKS(2000)); // Đợi một chút để log được ghi
    esp_restart();

    vTaskDelete(NULL); // Sẽ không bao giờ tới đây
}

// Hàm public để khởi tạo task OTA
void start_ota_firmware_update(const char *firmware_url_param) {
    set_ota_status(OTA_STATUS_IDLE); // Đặt lại trạng thái khi bắt đầu một lần OTA mới
    xTaskCreate(&ota_task, "ota_task", 8192, (void *)firmware_url_param, 5, NULL);
    ESP_LOGI(TAG, "OTA task created.");
}