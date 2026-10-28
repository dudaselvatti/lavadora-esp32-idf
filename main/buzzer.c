#include "buzzer.h"
#include "lavadora_config.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "LAVADORA";
typedef struct {
    bool ativo;
    bool ligado;
    int total;
    int concluidos;
    int duracao_ms;
    int intervalo_ms;
    int64_t proxima_ms;
    bool melodia;
    int indice_nota;
} Som;

// Pequeno jingle de conclusao, com notas musicais e pausas.
typedef struct {
    uint32_t frequencia_hz;
    int duracao_ms;
    int pausa_ms;
} NotaMelodia;

// Do5, Mi5, Sol5, Do6, Sol5, Do6.
static const NotaMelodia melodia_final[] = {
    {523, 180, 60},
    {659, 180, 60},
    {784, 240, 80},
    {1047, 430, 120},
    {784, 200, 60},
    {1047, 570, 0}
};

#define TOTAL_NOTAS ((int)(sizeof(melodia_final) / sizeof(melodia_final[0])))


static Som som = {0};


static void saida_som(bool ligado)
{
#if BUZZER_USAR_PWM
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0,
                                  ligado ? 512 : 0));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
#else
    ESP_ERROR_CHECK(gpio_set_level(BUZZER, ligado ? 1 : 0));
#endif
}

void buzzer_parar(void)
{
    som.ativo = false;
    som.ligado = false;
    som.melodia = false;
    saida_som(false);
}

void buzzer_iniciar_beeps(int quantidade, int duracao_ms, int intervalo_ms, int64_t agora)
{
    som.ativo = true;
    som.ligado = true;
    som.melodia = false;
    som.total = quantidade;
    som.concluidos = 0;
    som.duracao_ms = duracao_ms;
    som.intervalo_ms = intervalo_ms;
    som.proxima_ms = agora + duracao_ms;
#if BUZZER_USAR_PWM
    // Os beeps habituais continuam com a frequencia original de 2 kHz.
    ESP_ERROR_CHECK(ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, 2000));
#endif
    saida_som(true);
}

// Toca a primeira nota; as seguintes sao gerenciadas pelo loop principal.
void buzzer_iniciar_melodia_final(int64_t agora)
{
    buzzer_parar();
    som.ativo = true;
    som.melodia = true;
    som.ligado = true;
    som.indice_nota = 0;
    som.proxima_ms = agora + melodia_final[0].duracao_ms;
#if BUZZER_USAR_PWM
    ESP_ERROR_CHECK(ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0,
                                  melodia_final[0].frequencia_hz));
#endif
    saida_som(true);
}

void buzzer_atualizar(int64_t agora)
{
    if (!som.ativo || agora < som.proxima_ms) return;

    if (som.melodia) {
        if (som.ligado) {
            // Fim da nota atual: silencia durante a pausa entre notas.
            saida_som(false);
            som.ligado = false;
            som.proxima_ms = agora + melodia_final[som.indice_nota].pausa_ms;
        } else {
            som.indice_nota++;
            if (som.indice_nota >= TOTAL_NOTAS) {
                buzzer_parar();
                ESP_LOGI(TAG, "Melodia de finalizacao concluida");
                return;
            }
#if BUZZER_USAR_PWM
            ESP_ERROR_CHECK(ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0,
                                          melodia_final[som.indice_nota].frequencia_hz));
#endif
            saida_som(true);
            som.ligado = true;
            som.proxima_ms = agora + melodia_final[som.indice_nota].duracao_ms;
        }
        return;
    }

    // Padroes de beeps ja usados pelos outros estados da lavadora.
    if (som.ligado) {
        saida_som(false);
        som.ligado = false;
        som.concluidos++;
        som.proxima_ms = agora + som.intervalo_ms;
    } else if (som.concluidos < som.total) {
        saida_som(true);
        som.ligado = true;
        som.proxima_ms = agora + som.duracao_ms;
    } else {
        som.ativo = false;
    }
}



bool buzzer_ativo(void)
{
    return som.ativo;
}

bool buzzer_emitindo(void)
{
    return som.ativo && som.ligado;
}

void buzzer_inicializar(void)
{
#if BUZZER_USAR_PWM
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz = 2000,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    ledc_channel_config_t canal = {
        .gpio_num = BUZZER,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&canal));
#else
    gpio_config_t cfg_buzzer = {
        .pin_bit_mask = (1ULL << 25),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&cfg_buzzer));
    ESP_ERROR_CHECK(gpio_set_level(BUZZER, 0));
#endif
}
