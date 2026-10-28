#include "leds.h"
#include "lavadora_config.h"
#include "esp_err.h"

static const gpio_num_t leds[3] = {LED_AMARELO, LED_VERDE, LED_VERMELHO};

void leds_inicializar(void)
{
    gpio_config_t cfg_led = {
        .pin_bit_mask = (1ULL << 16) | (1ULL << 17) | (1ULL << 18),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&cfg_led));
    for (int i = 0; i < 3; i++) {
        ESP_ERROR_CHECK(gpio_set_level(leds[i], 1));
    }
}

// LEDs da PCB sao ativos em LOW.
void leds_exibir(const bool ligados[3])
{
    for (int i = 0; i < 3; i++) {
        ESP_ERROR_CHECK(gpio_set_level(leds[i], ligados[i] ? 0 : 1));
    }
}
