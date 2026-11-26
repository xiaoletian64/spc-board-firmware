# main/CMakeLists.txt — target-split: S3 keeps phone-hand lineage,
# P4 gets the adb-hub path (wifi/led/link/usb_ota join at M3).
if(CONFIG_IDF_TARGET_ESP32P4)
    # NOTE(M2): REQUIRES adb_host does not propagate include dirs on IDF6
    # (component rework pending) -> add its include path directly here.
    idf_component_register(SRCS "p4_main.c" "ota_guard.c"
                           INCLUDE_DIRS "." "../components/adb_host/include"
                           REQUIRES adb_host nvs_flash app_update
                                    esp_partition esp_timer)
else()
    idf_component_register(SRCS "main.c" "led.c" "link.c" "wifi.c"
                                "ota_guard.c" "usb_ota.c" "ota.cpp"
                           INCLUDE_DIRS "."
                           REQUIRES driver esp_timer esp_wifi nvs_flash
                                    esp_event esp_netif app_update
                                    esp_partition mbedtls esp_http_client)
endif()
