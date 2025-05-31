#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include <stdio.h>
#include <string.h>
#include <time.h> // >> MỚI: Thêm thư viện time.h

#include "inc/app_config.h"

// Định nghĩa cấu trúc dữ liệu cảm biến
typedef struct {
    float temperature;
    float humidity;
} sensor_data_t;

static const char *TAG = "LCD_TASK";

// Khai báo extern cho các biến toàn cục
extern EventGroupHandle_t wifi_event_group;
extern sensor_data_t g_display_sensor_data;
extern SemaphoreHandle_t g_display_sensor_data_mutex;
// >> MỚI: Khai báo extern cho các biến thời gian
extern struct tm g_current_timeinfo;
extern SemaphoreHandle_t g_current_time_mutex;
extern bool g_time_synchronized;


// Cấu hình I2C cho LCD
#define LCD_I2C_PORT        I2C_NUM_0
#define LCD_I2C_SDA_PIN     GPIO_NUM_21
#define LCD_I2C_SCL_PIN     GPIO_NUM_22
#define LCD_I2C_MASTER_FREQ_HZ 100000
#define LCD_I2C_ADDRESS     0x27

// Các bit điều khiển trên PCF8574
#define LCD_RS_BIT (1 << 0)
#define LCD_RW_BIT (1 << 1)
#define LCD_EN_BIT (1 << 2)
#define LCD_BL_BIT (1 << 3)

static uint8_t backlight_status = LCD_BL_BIT;

static i2c_master_bus_handle_t i2c_bus_handle = NULL;
static i2c_master_dev_handle_t i2c_dev_handle_lcd = NULL;

// --- Các hàm cấp thấp để điều khiển LCD (pcf8574_write_byte, lcd_pulse_enable,...) ---
// --- Giữ nguyên các hàm này, không thay đổi ---
static esp_err_t pcf8574_write_byte(uint8_t data) {
    if (i2c_dev_handle_lcd == NULL) {
        ESP_LOGE(TAG, "I2C device handle for LCD is not initialized!");
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = i2c_master_transmit(i2c_dev_handle_lcd, &data, 1, pdMS_TO_TICKS(1000));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PCF8574 write failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

static void lcd_pulse_enable(uint8_t data_val) {
    pcf8574_write_byte(data_val | LCD_EN_BIT);
    vTaskDelay(pdMS_TO_TICKS(1)); 
    pcf8574_write_byte(data_val & ~LCD_EN_BIT);
    vTaskDelay(pdMS_TO_TICKS(1)); 
}

static void lcd_send_nibble(uint8_t nibble, bool is_data_mode) {
    uint8_t pcf_data = 0;
    if (is_data_mode) {
        pcf_data |= LCD_RS_BIT;
    }
    pcf_data |= backlight_status;
    pcf_data |= (nibble << 4) & 0xF0;
    lcd_pulse_enable(pcf_data);
}

static void lcd_send_byte(uint8_t byte, bool is_data_mode) {
    lcd_send_nibble((byte >> 4) & 0x0F, is_data_mode);
    lcd_send_nibble(byte & 0x0F, is_data_mode);
}

void lcd_init_concrete() {
    ESP_LOGI(TAG, "Initializing LCD 1602A via I2C (New API)...");
    i2c_master_bus_config_t i2c_mst_config = {
        .i2c_port = LCD_I2C_PORT,
        .sda_io_num = LCD_I2C_SDA_PIN,
        .scl_io_num = LCD_I2C_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags.enable_internal_pullup = true
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_mst_config, &i2c_bus_handle));

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = LCD_I2C_ADDRESS,
        .scl_speed_hz = LCD_I2C_MASTER_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus_handle, &dev_cfg, &i2c_dev_handle_lcd));

    pcf8574_write_byte(LCD_BL_BIT); // Bật đèn nền
    vTaskDelay(pdMS_TO_TICKS(50)); 
    lcd_send_nibble(0x03, false); 
    vTaskDelay(pdMS_TO_TICKS(5));
    lcd_send_nibble(0x03, false);
    vTaskDelay(pdMS_TO_TICKS(1));
    lcd_send_nibble(0x03, false);
    vTaskDelay(pdMS_TO_TICKS(1));
    lcd_send_nibble(0x02, false); 
    vTaskDelay(pdMS_TO_TICKS(1));
    lcd_send_byte(0x28, false); 
    lcd_send_byte(0x0C, false); 
    lcd_send_byte(0x01, false); 
    vTaskDelay(pdMS_TO_TICKS(5)); 
    lcd_send_byte(0x06, false); 
    ESP_LOGI(TAG, "LCD Initialized.");
}

void lcd_clear_concrete() {
    lcd_send_byte(0x01, false); 
    vTaskDelay(pdMS_TO_TICKS(5)); 
}

void lcd_set_cursor_concrete(uint8_t row, uint8_t col) {
    uint8_t address = (row == 0) ? col : (col + 0x40);
    lcd_send_byte(0x80 | address, false); 
}

void lcd_print_string_concrete(const char* str) {
    while (*str) {
        lcd_send_byte((uint8_t)(*str), true); 
        str++;
    }
}
// ---------------------------------------------------------------------------------


void lcd_task(void *pvParameters) {
    ESP_LOGI(TAG, "LCD Task Started");
    lcd_init_concrete();

    sensor_data_t local_sensor_data = {0.0f, 0.0f};
    char line1_buffer[17]; 
    char line2_buffer[17];
    // >> MỚI: Thêm một buffer tạm để chứa nội dung gốc
    char content_buffer[17]; 

    static bool display_mode_is_wifi = true;

    lcd_set_cursor_concrete(0, 0);
    lcd_print_string_concrete("Xin chao!");
    vTaskDelay(pdMS_TO_TICKS(2000));
    lcd_clear_concrete();

    while (1) {
        // --- Cập nhật dòng 1: Dữ liệu cảm biến (giữ nguyên) ---
        if (xSemaphoreTake(g_display_sensor_data_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            local_sensor_data = g_display_sensor_data;
            xSemaphoreGive(g_display_sensor_data_mutex);
        }
        snprintf(line1_buffer, sizeof(line1_buffer), "T:%.1fC H:%.1f%%",
                 local_sensor_data.temperature, local_sensor_data.humidity);
        
        lcd_set_cursor_concrete(0, 0);
        lcd_print_string_concrete(line1_buffer);


        // --- Cập nhật dòng 2: Luân phiên giữa WiFi và Thời gian ---
        if (display_mode_is_wifi) {
            bool wifi_is_connected = (wifi_event_group && (xEventGroupGetBits(wifi_event_group) & WIFI_CONNECTED_BIT));
            // Tạo nội dung gốc vào buffer tạm
            snprintf(content_buffer, sizeof(content_buffer), "WiFi: %s",
                     wifi_is_connected ? "Online" : "Offline");
        } else {
            if (g_time_synchronized) {
                struct tm time_snapshot;
                if (xSemaphoreTake(g_current_time_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                    time_snapshot = g_current_timeinfo;
                    xSemaphoreGive(g_current_time_mutex);
                }
                // Tạo nội dung gốc vào buffer tạm
                snprintf(content_buffer, sizeof(content_buffer), "Time: %02d:%02d:%02d",
                         time_snapshot.tm_hour, time_snapshot.tm_min, time_snapshot.tm_sec);
            } else {
                // Tạo nội dung gốc vào buffer tạm
                snprintf(content_buffer, sizeof(content_buffer), "Time: Not Sync");
            }
        }
        
        // >> THAY ĐỔI QUAN TRỌNG: Đệm chuỗi bằng khoảng trắng để đủ 16 ký tự
        // Định dạng "%-16s" sẽ căn lề trái và chèn thêm khoảng trắng vào cuối
        snprintf(line2_buffer, sizeof(line2_buffer), "%-16s", content_buffer);
        
        lcd_set_cursor_concrete(1, 0);
        lcd_print_string_concrete(line2_buffer);
        
        // Đảo chế độ hiển thị cho lần lặp tiếp theo
        display_mode_is_wifi = !display_mode_is_wifi;

        vTaskDelay(pdMS_TO_TICKS(APP_LCD_UPDATE_INTERVAL_MS)); // Lấy từ app_config.h
    }
}