#include "botoes.h"
#include "lavadora_config.h"
#include "esp_err.h"

typedef struct
{
    gpio_num_t pino;
    int estavel;
    bool pendente;
    int64_t mudou_em_ms;
} Botao;

static Botao botao1 = {.pino = BOTAO_1};
static Botao botao2 = {.pino = BOTAO_2};

static volatile uint32_t eventos_botoes = 0;

static void isr_botao1(void *arg)
{
    (void)arg;
    __atomic_fetch_or(&eventos_botoes, 1U, __ATOMIC_RELAXED);
}

static void isr_botao2(void *arg)
{
    (void)arg;
    __atomic_fetch_or(&eventos_botoes, 2U, __ATOMIC_RELAXED);
}

void botoes_inicializar(void)
{
    gpio_config_t cfg_btn = {
        .pin_bit_mask = (1ULL << 32) | (1ULL << 33),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_ANYEDGE};
    ESP_ERROR_CHECK(gpio_config(&cfg_btn));
    ESP_ERROR_CHECK(gpio_install_isr_service(0));

    ESP_ERROR_CHECK(
        gpio_isr_handler_add(BOTAO_1, isr_botao1, NULL));

    ESP_ERROR_CHECK(
        gpio_isr_handler_add(BOTAO_2, isr_botao2, NULL));
}

static bool pressionou(Botao *botao, uint32_t mascara, int64_t agora)
{
    // Verifica e consome a interrupcao deste botao.
    uint32_t eventos = __atomic_fetch_and(
        &eventos_botoes, ~mascara, __ATOMIC_ACQ_REL
    );

    // Uma borda foi detectada.
    if (eventos & mascara) {
        botao->mudou_em_ms = agora;
        botao->pendente = true;
    }

    // Nenhuma mudanca aguardando validacao.
    if (!botao->pendente) {
        return false;
    }

    // Aguarda 50 ms desde a ultima borda observada.
    if (agora - botao->mudou_em_ms < DEBOUNCE_MS) {
        return false;
    }

    // Valida o nivel somente depois do debounce.
    botao->pendente = false;
    int leitura = gpio_get_level(botao->pino);

    if (leitura != botao->estavel) {
        botao->estavel = leitura;
        return leitura == 1;
    }

    return false;
}

bool botao_1_pressionado(int64_t agora_ms)
{
    return pressionou(&botao1, 1U, agora_ms);
}

bool botao_2_pressionado(int64_t agora_ms)
{
    return pressionou(&botao2, 2U, agora_ms);
}
