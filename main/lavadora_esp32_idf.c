#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "nvs.h"

#define LED_AMARELO    GPIO_NUM_16
#define LED_VERDE      GPIO_NUM_17
#define LED_VERMELHO   GPIO_NUM_18
#define BOTAO_1        GPIO_NUM_32
#define BOTAO_2        GPIO_NUM_33
#define BUZZER         GPIO_NUM_25
#define ADC_POT        ADC_CHANNEL_6  // GPIO34 / ADC1

// 1 = PWM (buzzer do Wokwi); 0 = acionamento digital (para teste futuro)
#define BUZZER_USAR_PWM 1
#define DEBOUNCE_MS    50
#define PISCAR_MS      350
#define HISTERESE_ADC  50
#define CHECKPOINT_MS  2000
#define NVS_ASSINATURA 0x4C415641UL  // "LAVA"
#define NVS_VERSAO     1

static const char *TAG = "LAVADORA";
static const gpio_num_t leds[3] = {LED_AMARELO, LED_VERDE, LED_VERMELHO};
static const char *nomes_programas[3] = {"RAPIDO", "NORMAL", "INTENSO"};
static const char *nomes_etapas[3] = {"LAVAGEM", "ENXAGUE", "CENTRIFUGACAO"};
static const int tempos[3][3] = {{7, 4, 4}, {12, 7, 6}, {18, 9, 8}};
static const int totais[3] = {15, 25, 35};

typedef enum {
    DESLIGADA, SELECAO, CONFIRMADO, EXECUTANDO, PAUSADO, FINALIZANDO, RECUPERACAO
} Estado;

typedef struct {
    gpio_num_t pino;
    int ultima_leitura;
    int estavel;
    int64_t mudou_em_ms;
} Botao;

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

typedef struct {
    uint32_t assinatura;
    uint32_t versao;
    uint32_t programa;
    uint32_t etapa;
    uint32_t restante_ms;
    uint32_t estava_pausado;
} RegistroNVS;

static nvs_handle_t handle_nvs_lavadora;
static RegistroNVS registro_recuperado;
static int64_t ultimo_checkpoint_ms = 0;

static Estado estado = DESLIGADA;
static int programa = 0;
static int etapa = 0;
static int adc_filtrado = 0;
static int64_t ultima_leitura_adc_ms = 0;
static int64_t ultimo_tempo_ms = 0;
static int64_t tempo_etapa_ms = 0;
static int ultimo_segundo_logado = -1;
static Som som = {0};
static adc_oneshot_unit_handle_t adc;

static int64_t tempo_ms(void)
{
    return esp_timer_get_time() / 1000;
}

// =====================================================
// PERSISTENCIA DO CICLO NA MEMORIA FLASH (NVS)
// =====================================================

static bool salvar_ciclo(bool pausado, int64_t agora)
{
    int64_t duracao_ms = (int64_t)tempos[programa][etapa] * 1000;
    int64_t restante = duracao_ms - tempo_etapa_ms;
    if (restante < 1) restante = 1;
    if (restante > duracao_ms) restante = duracao_ms;

    // Um unico blob evita misturar programa, etapa e tempo de
    // checkpoints diferentes em caso de interrupcao da escrita.
    RegistroNVS registro = {
        .assinatura = NVS_ASSINATURA,
        .versao = NVS_VERSAO,
        .programa = (uint32_t)programa,
        .etapa = (uint32_t)etapa,
        .restante_ms = (uint32_t)restante,
        .estava_pausado = pausado ? 1U : 0U
    };

    esp_err_t erro = nvs_set_blob(handle_nvs_lavadora, "ciclo", &registro, sizeof(registro));
    if (erro == ESP_OK) erro = nvs_commit(handle_nvs_lavadora);

    if (erro != ESP_OK) {
        ESP_LOGE(TAG, "NVS: erro ao salvar ciclo: %s", esp_err_to_name(erro));
        return false;
    }

    ultimo_checkpoint_ms = agora;
    ESP_LOGI(TAG, "NVS salvo: %s | %s | restam %u ms | %s",
             nomes_programas[programa], nomes_etapas[etapa],
             (unsigned)registro.restante_ms,
             pausado ? "PAUSADO" : "EXECUTANDO");
    return true;
}

static bool limpar_ciclo_salvo(void)
{
    esp_err_t erro = nvs_erase_key(handle_nvs_lavadora, "ciclo");
    if (erro == ESP_ERR_NVS_NOT_FOUND) return true;
    if (erro == ESP_OK) erro = nvs_commit(handle_nvs_lavadora);

    if (erro != ESP_OK) {
        ESP_LOGE(TAG, "NVS: erro ao apagar ciclo: %s", esp_err_to_name(erro));
        return false;
    }

    ESP_LOGI(TAG, "NVS: ciclo salvo removido");
    return true;
}

static bool registro_valido(const RegistroNVS *r)
{
    if (r->assinatura != NVS_ASSINATURA || r->versao != NVS_VERSAO) return false;
    if (r->programa >= 3 || r->etapa >= 3 || r->estava_pausado > 1) return false;
    if (r->restante_ms == 0) return false;
    return r->restante_ms <= (uint32_t)(tempos[r->programa][r->etapa] * 1000);
}

static void inicializar_nvs_e_recuperacao(void)
{
    // Nao apagamos a particao automaticamente em caso de erro:
    // ela pode conter outros dados da ESP32.
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(nvs_open("lavadora", NVS_READWRITE, &handle_nvs_lavadora));

    size_t tamanho = 0;
    esp_err_t erro = nvs_get_blob(handle_nvs_lavadora, "ciclo", NULL, &tamanho);

    if (erro == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "NVS: nenhum ciclo interrompido encontrado");
        return;
    }
    ESP_ERROR_CHECK(erro);

    if (tamanho != sizeof(registro_recuperado)) {
        ESP_LOGW(TAG, "NVS: registro antigo/incompativel; descartando");
        ESP_ERROR_CHECK(limpar_ciclo_salvo() ? ESP_OK : ESP_FAIL);
        return;
    }

    ESP_ERROR_CHECK(nvs_get_blob(handle_nvs_lavadora, "ciclo", &registro_recuperado, &tamanho));

    if (!registro_valido(&registro_recuperado)) {
        ESP_LOGW(TAG, "NVS: registro invalido; descartando");
        ESP_ERROR_CHECK(limpar_ciclo_salvo() ? ESP_OK : ESP_FAIL);
        return;
    }

    programa = (int)registro_recuperado.programa;
    etapa = (int)registro_recuperado.etapa;
    tempo_etapa_ms = (int64_t)tempos[programa][etapa] * 1000
                      - registro_recuperado.restante_ms;
    estado = RECUPERACAO;

    ESP_LOGW(TAG, "CICLO INTERROMPIDO DETECTADO!");
    ESP_LOGW(TAG, "Programa: %s | Etapa: %s | restavam %u ms",
             nomes_programas[programa], nomes_etapas[etapa],
             (unsigned)registro_recuperado.restante_ms);
    ESP_LOGW(TAG, "Antes do reset: %s",
             registro_recuperado.estava_pausado ? "PAUSADO" : "EXECUTANDO");
    ESP_LOGW(TAG, "LED vermelho piscando. B1=RETOMAR, B2=CANCELAR");
}

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

static void parar_som(void)
{
    som.ativo = false;
    som.ligado = false;
    som.melodia = false;
    saida_som(false);
}

static void iniciar_som(int quantidade, int duracao_ms, int intervalo_ms, int64_t agora)
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
static void iniciar_melodia_final(int64_t agora)
{
    parar_som();
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

static void atualizar_som(int64_t agora)
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
                parar_som();
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

static bool pressionou(Botao *botao, int64_t agora)
{
    int leitura = gpio_get_level(botao->pino);
    if (leitura != botao->ultima_leitura) {
        botao->ultima_leitura = leitura;
        botao->mudou_em_ms = agora;
    }
    if (leitura != botao->estavel && agora - botao->mudou_em_ms >= DEBOUNCE_MS) {
        botao->estavel = leitura;
        return leitura == 1;  // Apenas ao pressionar, nunca ao soltar
    }
    return false;
}

static int programa_adc(int valor, int atual)
{
    if (atual < 0) {
        if (valor < 1365) return 0;
        if (valor < 2730) return 1;
        return 2;
    }
    if (atual == 0) {
        if (valor > 2730 + HISTERESE_ADC) return 2;
        if (valor > 1365 + HISTERESE_ADC) return 1;
    } else if (atual == 1) {
        if (valor < 1365 - HISTERESE_ADC) return 0;
        if (valor > 2730 + HISTERESE_ADC) return 2;
    } else {
        if (valor < 1365 - HISTERESE_ADC) return 0;
        if (valor < 2730 - HISTERESE_ADC) return 1;
    }
    return atual;
}

static void entrar_selecao(int64_t agora)
{
    estado = SELECAO;
    int soma = 0;
    for (int i = 0; i < 8; i++) {
        int leitura = 0;
        ESP_ERROR_CHECK(adc_oneshot_read(adc, ADC_POT, &leitura));
        soma += leitura;
    }
    adc_filtrado = soma / 8;
    programa = programa_adc(adc_filtrado, -1);
    ultima_leitura_adc_ms = agora;
    ESP_LOGI(TAG, "SELECAO: %s (%d s), ADC=%d",
             nomes_programas[programa], totais[programa], adc_filtrado);
}

static void atualizar_selecao(int64_t agora)
{
    if (estado != SELECAO || agora - ultima_leitura_adc_ms < 100) return;
    ultima_leitura_adc_ms = agora;
    int leitura = 0;
    ESP_ERROR_CHECK(adc_oneshot_read(adc, ADC_POT, &leitura));
    adc_filtrado = (7 * adc_filtrado + leitura) / 8;
    int novo = programa_adc(adc_filtrado, programa);
    if (novo != programa) {
        programa = novo;
        ESP_LOGI(TAG, "Programa selecionado: %s (%d s), ADC=%d",
                 nomes_programas[programa], totais[programa], adc_filtrado);
    }
}

static int restante_segundos(void)
{
    int64_t faltam = (int64_t)tempos[programa][etapa] * 1000 - tempo_etapa_ms;
    if (faltam < 0) faltam = 0;
    return (int)((faltam + 999) / 1000);
}

static void atualizar_ciclo(int64_t agora)
{
    if (estado != EXECUTANDO) return;
    int64_t delta = agora - ultimo_tempo_ms;
    if (delta > 0) tempo_etapa_ms += delta;
    ultimo_tempo_ms = agora;

    int64_t duracao = (int64_t)tempos[programa][etapa] * 1000;
    if (tempo_etapa_ms >= duracao) {
        if (etapa < 2) {
            etapa++;
            tempo_etapa_ms = 0;
            ultimo_segundo_logado = -1;
            ESP_LOGI(TAG, "Nova etapa: %s (%d s)",
                     nomes_etapas[etapa], tempos[programa][etapa]);
            salvar_ciclo(false, agora);
            iniciar_som(1, 180, 150, agora);
        } else {
            estado = FINALIZANDO;
            limpar_ciclo_salvo();
            ESP_LOGI(TAG, "Ciclo concluido! Tocando melodia de finalizacao.");
            iniciar_melodia_final(agora);
        }
        return;
    }

    int restante = restante_segundos();
    if (restante != ultimo_segundo_logado) {
        ultimo_segundo_logado = restante;
        ESP_LOGI(TAG, "%s: faltam %d s", nomes_etapas[etapa], restante);
    }

    if (agora - ultimo_checkpoint_ms >= CHECKPOINT_MS) {
        salvar_ciclo(false, agora);
    }
}

static void tratar_botao_1(int64_t agora)
{
    switch (estado) {
        case DESLIGADA:
            ESP_LOGI(TAG, "Lavadora ligada");
            entrar_selecao(agora);
            break;
        case SELECAO:
            estado = CONFIRMADO;
            ESP_LOGI(TAG, "Confirmado: %s (%d s)", nomes_programas[programa], totais[programa]);
            break;
        case CONFIRMADO:
            etapa = 0;
            tempo_etapa_ms = 0;
            ultimo_tempo_ms = agora;
            ultimo_segundo_logado = -1;
            // Nao iniciamos sem conseguir gravar o primeiro checkpoint.
            if (!salvar_ciclo(false, agora)) {
                ESP_LOGE(TAG, "Inicio impedido: NVS indisponivel");
                break;
            }
            estado = EXECUTANDO;
            ESP_LOGI(TAG, "INICIO: %s | Etapa: %s (%d s)",
                     nomes_programas[programa], nomes_etapas[etapa], tempos[programa][etapa]);
            iniciar_som(1, 180, 150, agora);
            break;
        case EXECUTANDO:
            estado = PAUSADO;
            salvar_ciclo(true, agora);
            ESP_LOGI(TAG, "PAUSADO: %s, faltam %d s", nomes_etapas[etapa], restante_segundos());
            iniciar_som(2, 150, 150, agora);
            break;
        case PAUSADO:
            ultimo_tempo_ms = agora;
            if (!salvar_ciclo(false, agora)) {
                ESP_LOGE(TAG, "Retomada impedida: NVS indisponivel");
                break;
            }
            estado = EXECUTANDO;
            ESP_LOGI(TAG, "RETOMADO: %s, faltam %d s", nomes_etapas[etapa], restante_segundos());
            iniciar_som(1, 180, 150, agora);
            break;
        case RECUPERACAO:
            parar_som();
            // programa, etapa e tempo_etapa_ms vieram da NVS no boot.
            ultimo_tempo_ms = agora;
            ultimo_segundo_logado = -1;
            if (!salvar_ciclo(false, agora)) {
                ESP_LOGE(TAG, "Recuperacao impedida: NVS indisponivel");
                iniciar_som(2, 160, 160, agora);
                break;
            }
            estado = EXECUTANDO;
            ESP_LOGI(TAG, "CICLO RECUPERADO: %s | %s | faltam %d s",
                     nomes_programas[programa], nomes_etapas[etapa],
                     restante_segundos());
            iniciar_som(1, 180, 150, agora);
            break;
        case FINALIZANDO:
            break;  // Ignorar comandos durante a melodia final
    }
}

static void tratar_botao_2(int64_t agora)
{
    switch (estado) {
        case SELECAO:
            estado = DESLIGADA;
            parar_som();
            ESP_LOGI(TAG, "Lavadora desligada");
            break;
        case CONFIRMADO:
            ESP_LOGI(TAG, "Confirmacao desfeita");
            entrar_selecao(agora);
            break;
        case EXECUTANDO:
        case PAUSADO:
            ESP_LOGI(TAG, "Ciclo CANCELADO");
            limpar_ciclo_salvo();
            entrar_selecao(agora);
            iniciar_som(1, 700, 150, agora);
            break;
        case RECUPERACAO:
            parar_som();
            ESP_LOGI(TAG, "Recuperacao CANCELADA pelo usuario");
            limpar_ciclo_salvo();
            entrar_selecao(agora);
            iniciar_som(1, 700, 150, agora);
            break;
        case DESLIGADA:
        case FINALIZANDO:
            break;
    }
}

static void atualizar_leds(int64_t agora)
{
    bool on[3] = {false, false, false};
    bool piscar = ((agora / PISCAR_MS) % 2) == 0;
    switch (estado) {
        case SELECAO:
            on[0] = piscar;  // Amarelo sempre pisca na selecao
            if (programa == 1) on[1] = true;
            if (programa == 2) on[2] = true;
            break;
        case CONFIRMADO:
            on[programa] = true;
            break;
        case EXECUTANDO:
            on[etapa] = true;
            break;
        case PAUSADO:
            on[etapa] = piscar;
            break;
        case RECUPERACAO:
            on[2] = piscar;  // Vermelho pisca ate escolha do usuario
            break;
        case FINALIZANDO:
            // LEDs acompanham as notas da melodia.
            if (som.ativo && som.ligado) {
                on[0] = on[1] = on[2] = true;
            }
            break;
        case DESLIGADA:
            break;
    }
    // Os LEDs da placa acendem em LOW
    for (int i = 0; i < 3; i++) {
        ESP_ERROR_CHECK(gpio_set_level(leds[i], on[i] ? 0 : 1));
    }
}

static void configurar_perifericos(void)
{
    gpio_config_t cfg_led = {
        .pin_bit_mask = (1ULL << 16) | (1ULL << 17) | (1ULL << 18),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&cfg_led));
    for (int i = 0; i < 3; i++) ESP_ERROR_CHECK(gpio_set_level(leds[i], 1));

    gpio_config_t cfg_btn = {
        .pin_bit_mask = (1ULL << 32) | (1ULL << 33),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&cfg_btn));

    adc_oneshot_unit_init_cfg_t cfg_adc = {.unit_id = ADC_UNIT_1};
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&cfg_adc, &adc));
    adc_oneshot_chan_cfg_t cfg_canal = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc, ADC_POT, &cfg_canal));

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

void app_main(void)
{
    configurar_perifericos();
    inicializar_nvs_e_recuperacao();
    Botao b1 = {.pino = BOTAO_1};
    Botao b2 = {.pino = BOTAO_2};
    if (estado == RECUPERACAO) {
        iniciar_som(2, 160, 160, tempo_ms());
    } else {
        ESP_LOGI(TAG, "Lavadora pronta! Botao 1 para ligar.");
    }
    ESP_LOGI(TAG, "Programas: rapido 15s, normal 25s, intenso 35s");
    atualizar_leds(tempo_ms());

    while (1) {
        int64_t agora = tempo_ms();
        atualizar_som(agora);
        atualizar_ciclo(agora);

        if (estado == FINALIZANDO && !som.ativo) {
            ESP_LOGI(TAG, "Sinalizacao concluida. Retornando a selecao.");
            entrar_selecao(agora);
        }
        atualizar_selecao(agora);

        bool evento_b1 = pressionou(&b1, agora);
        bool evento_b2 = pressionou(&b2, agora);
        // Se os dois forem pressionados juntos, o cancelamento tem prioridade
        if (evento_b2) tratar_botao_2(agora);
        else if (evento_b1) tratar_botao_1(agora);

        atualizar_leds(agora);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}