idf_component_register(SRCS "main.cpp" "stim_controller.cpp" "board_config.cpp"
                            "camera_server.cpp" "wifi_manager.cpp" "espnow_receiver.cpp"
                            "command_protocol.cpp"
                    INCLUDE_DIRS "." "include"
                    REQUIRES 
                        esp_driver_gpio
                        esp_timer
                        esp32-camera
                        esp_http_server
                        esp_wifi
                        nvs_flash
                        mdns
                        esp_event
                        esp_psram
                        spi_flash)
