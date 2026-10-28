#include "botoes.h"
#include "lavadora_config.h"
#include "esp_err.h"

typedef struct {
    gpio_num_t pino;
    int ultima_leitura;
    int estavel;
    int64_t mudou_em_ms;
} Botao;

static Botao botao1 = {.pino = BOTAO_1};
static Botao botao2 = {.pino = BOTAO_2};

void botoes_inicializar(void)
{
    gpio_config_t cfg_btn = {
        .pin_bit_mask = (1ULL << 32) | (1ULL << 33),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&cfg_btn));
}

// Preserva a deteccao de borda com debounce de 50 ms.
static bool pressionou(Botao *botao, int64_t agora)
{
    int leitura = gpio_get_level(botao->pino);
    if (leitura != botao->ultima_leitura) {
        botao->ultima_leitura = leitura;
        botao->mudou_em_ms = agora;
    }
    if (leitura != botao->estavel && agora - botao->mudou_em_ms >= DEBOUNCE_MS) {
        botao->estavel = leitura;
        return leitura == 1;
    }
    return false;
}

bool botao_1_pressionado(int64_t agora_ms)
{
    return pressionou(&botao1, agora_ms);
}

bool botao_2_pressionado(int64_t agora_ms)
{
    return pressionou(&botao2, agora_ms);
}
