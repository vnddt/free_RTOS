idf_component_register(SRCS "src/main.c"
                            "src/sensor_task.c"
                            "src/wifi_task.c"
                            "src/mqtt_task.c"
                            "src/ntp_task.c"
                            "src/ota_task.c"
                            "src/lcd_task.c"
                    INCLUDE_DIRS "."
                    REQUIRES    nvs_flash       esp_wifi        esp_netif       driver 
                                esp_event       log mqtt        esp_driver_gpio 
                                esp_netif       esp_timer       esp_driver_i2c
                                esp_http_client         esp_https_ota		esp_system	esp_common      esp_driver_i2c
                    PRIV_REQUIRES       app_update  
                                        u8g2        
                    )
