// ota_client.h
#ifndef OTA_CLIENT_H
#define OTA_CLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Bắt đầu quá trình cập nhật OTA.
 *
 * Hàm này sẽ tạo một task FreeRTOS mới để thực hiện việc tải và cài đặt firmware.
 * Đảm bảo Wi-Fi đã được kết nối trước khi gọi hàm này.
 *
 * @param firmware_url URL trỏ đến file firmware .bin trên server.
 * URL này phải tồn tại trong suốt thời gian task OTA hoạt động.
 * Sử dụng một chuỗi ký tự (string literal) hoặc bộ nhớ được cấp phát động
 * mà không bị giải phóng cho đến khi task OTA hoàn thành hoặc bị xóa.
 */
void start_ota_firmware_update(const char *firmware_url);

#ifdef __cplusplus
}
#endif

#endif // OTA_CLIENT_H