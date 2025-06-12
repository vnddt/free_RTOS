#ifndef APP_CONFIG_H
#define APP_CONFIG_H

// Cấu hình WiFi
#define WIFI_SSID      "SpaceX"
// #define WIFI_SSID      "HOANG PHONG"
// #define WIFI_SSID      "TapDD"
#define WIFI_PASSWORD  "0364429807"
// #define WIFI_PASSWORD  "11111111"


#define DHT11_GPIO_PIN  GPIO_NUM_4 // Chân GPIO kết nối với cảm biến DHT11


// Cấu hình MQTT Broker
#define MQTT_BROKER_URL "mqtt://test.mosquitto.org" // Ví dụ: "mqtt://test.mosquitto.org"
#define MQTT_TOPIC      "esp32/dht_data"



// Kích thước hàng đợi dữ liệu cảm biến
#define SENSOR_DATA_QUEUE_SIZE 5


#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// Cấu hình OTA
#define OTA_BUFF_SIZE 2048 // Kích thước buffer để đọc dữ liệu HTTP và ghi vào flash

// URL của file firmware trên server HTTP của bạn
// Thay thế bằng URL thực tế của bạn (từ Bước 1.2)
// Ví dụ: "http://192.168.1.100:8000/my_app.bin"
// HOẶC: URL của file version.txt nếu bạn muốn đọc URL từ đó
#define FIRMWARE_UPGRADE_URL "http://192.168.0.101:8000/freeRTOS.bin"

#define APP_LCD_UPDATE_INTERVAL_MS 3000 // Thời gian cập nhật LCD (ms)
#define APP_SENSOR_UPDATE_INTERVAL_MS 5000 // Thời gian cập nhật cảm biến (ms)


#endif // APP_CONFIG_H