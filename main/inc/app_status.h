// inc/app_status.h
#ifndef APP_STATUS_H
#define APP_STATUS_H

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Enum định nghĩa các trạng thái có thể có của quá trình OTA
typedef enum {
    OTA_STATUS_IDLE,                 // Không có hoạt động OTA
    OTA_STATUS_STARTING,             // Bắt đầu quá trình OTA
    OTA_STATUS_FIRMWARE_CHECK,       // (Tùy chọn) Đang kiểm tra phiên bản firmware
    OTA_STATUS_DOWNLOADING,          // Đang tải firmware
    OTA_STATUS_WRITE_FLASH,          // Đang ghi firmware vào flash
    OTA_STATUS_VALIDATING,           // Đang xác thực firmware
    OTA_STATUS_SUCCESS_RESTARTING,   // OTA thành công, chuẩn bị khởi động lại
    OTA_STATUS_FAILED_PARTITION,     // Lỗi: Không tìm thấy phân vùng OTA hợp lệ
    OTA_STATUS_FAILED_HTTP_CONN,     // Lỗi: Kết nối HTTP thất bại
    OTA_STATUS_FAILED_BEGIN,         // Lỗi: esp_ota_begin thất bại
    OTA_STATUS_FAILED_READ,          // Lỗi: Đọc dữ liệu HTTP thất bại
    OTA_STATUS_FAILED_WRITE,         // Lỗi: Ghi dữ liệu OTA thất bại
    OTA_STATUS_FAILED_END_VALIDATE,  // Lỗi: esp_ota_end hoặc xác thực thất bại
    OTA_STATUS_FAILED_SET_BOOT,      // Lỗi: esp_ota_set_boot_partition thất bại
    OTA_STATUS_NO_UPDATE_AVAILABLE   // (Tùy chọn) Không có bản cập nhật mới
} ota_status_t;

// Khai báo biến toàn cục cho trạng thái OTA và mutex bảo vệ
extern ota_status_t g_ota_status;
extern SemaphoreHandle_t g_ota_status_mutex;

// Khai báo hàm chuyển đổi enum trạng thái OTA sang chuỗi để hiển thị
const char* ota_status_to_string(ota_status_t status);

#endif // APP_STATUS_H