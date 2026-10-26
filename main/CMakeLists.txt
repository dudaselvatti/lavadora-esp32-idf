idf_component_register(
    SRCS "lavadora_esp32_idf.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        esp_driver_gpio
        esp_driver_ledc
        esp_adc
        esp_timer
        freertos
        log
        nvs_flash
)
