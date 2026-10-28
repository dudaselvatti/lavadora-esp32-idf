#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "leds.h"
#include "botoes.h"
#include "potenciometro.h"
#include "buzzer.h"
#include "lavadora.h"

void app_main(void)
{
    // Mesma sequencia de inicializacao da versao monolitica validada.
    leds_inicializar();
    botoes_inicializar();
    potenciometro_inicializar();
    buzzer_inicializar();
    lavadora_inicializar();

    while (1) {
        int64_t agora = esp_timer_get_time() / 1000;
        lavadora_atualizar(agora);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
