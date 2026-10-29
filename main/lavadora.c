#include <stdbool.h>
#include <stdint.h>
#include "lavadora.h"
#include "lavadora_config.h"
#include "programas.h"
#include "buzzer.h"
#include "botoes.h"
#include "esp_timer.h"
#include "potenciometro.h"
#include "leds.h"
#include "nvs_ciclo.h"
#include "esp_log.h"

static const char *TAG = "LAVADORA";

typedef enum
{
    DESLIGADA,
    SELECAO,
    CONFIRMADO,
    EXECUTANDO,
    PAUSADO,
    FINALIZANDO,
    RECUPERACAO
} Estado;

static Estado estado = DESLIGADA;
static uint8_t programa = 0;
static uint8_t etapa = 0;
static uint16_t adc_filtrado = 0;
static int8_t ultimo_segundo_logado = -1;
static int64_t ultima_leitura_adc_ms = 0;
static int64_t ultimo_tempo_ms = 0;
static int64_t tempo_etapa_ms = 0;
static int64_t ultimo_checkpoint_ms = 0;

static int64_t tempo_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static bool salvar_checkpoint(bool pausado, int64_t agora)
{
    bool ok = nvs_ciclo_salvar(programa, etapa, tempo_etapa_ms, pausado);
    if (ok)
        ultimo_checkpoint_ms = agora;
    return ok;
}

static void entrar_selecao(int64_t agora)
{
    estado = SELECAO;

    adc_filtrado = potenciometro_ler();
    
    programa = potenciometro_selecionar_programa(adc_filtrado, -1);
    ultima_leitura_adc_ms = agora;
    ESP_LOGI(TAG, "SELECAO: %s (%d s), ADC=%d",
             nomes_programas[programa], totais[programa], adc_filtrado);
}

static void atualizar_selecao(int64_t agora)
{
    if (estado != SELECAO || agora - ultima_leitura_adc_ms < 100)
        return;
    ultima_leitura_adc_ms = agora;
    adc_filtrado = potenciometro_ler();
    int novo = potenciometro_selecionar_programa(adc_filtrado, programa);
    if (novo != programa)
    {
        programa = novo;
        ESP_LOGI(TAG, "Programa selecionado: %s (%d s), ADC=%d",
                 nomes_programas[programa], totais[programa], adc_filtrado);
    }
}

static int restante_segundos(void)
{
    int64_t faltam = (int64_t)tempos[programa][etapa] * 1000 - tempo_etapa_ms;
    if (faltam < 0)
        faltam = 0;
    return (int)((faltam + 999) / 1000);
}

static void atualizar_ciclo(int64_t agora)
{
    if (estado != EXECUTANDO)
        return;
    int64_t delta = agora - ultimo_tempo_ms;
    if (delta > 0)
        tempo_etapa_ms += delta;
    ultimo_tempo_ms = agora;

    int64_t duracao = (int64_t)tempos[programa][etapa] * 1000;
    if (tempo_etapa_ms >= duracao)
    {
        if (etapa < 2)
        {
            etapa++;
            tempo_etapa_ms = 0;
            ultimo_segundo_logado = -1;
            ESP_LOGI(TAG, "Nova etapa: %s (%d s)",
                     nomes_etapas[etapa], tempos[programa][etapa]);
            salvar_checkpoint(false, agora);
            buzzer_iniciar_beeps(1, 180, 150, agora);
        }
        else
        {
            estado = FINALIZANDO;
            nvs_ciclo_limpar();
            ESP_LOGI(TAG, "Ciclo concluido! Tocando melodia de finalizacao.");
            buzzer_iniciar_melodia_final(agora);
        }
        return;
    }

    int restante = restante_segundos();
    if (restante != ultimo_segundo_logado)
    {
        ultimo_segundo_logado = restante;
        ESP_LOGI(TAG, "%s: faltam %d s", nomes_etapas[etapa], restante);
    }

    if (agora - ultimo_checkpoint_ms >= CHECKPOINT_MS)
    {
        salvar_checkpoint(false, agora);
    }
}

static void tratar_botao_1(int64_t agora)
{
    switch (estado)
    {
    case DESLIGADA:
        ESP_LOGI(TAG, "Lavadora ligada");
        entrar_selecao(agora);
        buzzer_iniciar_melodia_inicio(agora);
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
        if (!salvar_checkpoint(false, agora))
        {
            ESP_LOGE(TAG, "Inicio impedido: NVS indisponivel");
            break;
        }
        estado = EXECUTANDO;
        ESP_LOGI(TAG, "INICIO: %s | Etapa: %s (%d s)",
                 nomes_programas[programa], nomes_etapas[etapa], tempos[programa][etapa]);
        buzzer_iniciar_beeps(1, 180, 150, agora);
        break;
    case EXECUTANDO:
        estado = PAUSADO;
        salvar_checkpoint(true, agora);
        ESP_LOGI(TAG, "PAUSADO: %s, faltam %d s", nomes_etapas[etapa], restante_segundos());
        buzzer_iniciar_beeps(2, 150, 150, agora);
        break;
    case PAUSADO:
        ultimo_tempo_ms = agora;
        if (!salvar_checkpoint(false, agora))
        {
            ESP_LOGE(TAG, "Retomada impedida: NVS indisponivel");
            break;
        }
        estado = EXECUTANDO;
        ESP_LOGI(TAG, "RETOMADO: %s, faltam %d s", nomes_etapas[etapa], restante_segundos());
        buzzer_iniciar_beeps(1, 180, 150, agora);
        break;
    case RECUPERACAO:
        buzzer_parar();
        // programa, etapa e tempo_etapa_ms vieram da NVS no boot.
        ultimo_tempo_ms = agora;
        ultimo_segundo_logado = -1;
        if (!salvar_checkpoint(false, agora))
        {
            ESP_LOGE(TAG, "Recuperacao impedida: NVS indisponivel");
            buzzer_iniciar_beeps(2, 160, 160, agora);
            break;
        }
        estado = EXECUTANDO;
        ESP_LOGI(TAG, "CICLO RECUPERADO: %s | %s | faltam %d s",
                 nomes_programas[programa], nomes_etapas[etapa],
                 restante_segundos());
        buzzer_iniciar_beeps(1, 180, 150, agora);
        break;
    case FINALIZANDO:
        break; // Ignorar comandos durante a melodia final
    }
}

static void tratar_botao_2(int64_t agora)
{
    switch (estado)
    {
    case SELECAO:
        estado = DESLIGADA;
        buzzer_parar();
        ESP_LOGI(TAG, "Lavadora desligada");
        break;
    case CONFIRMADO:
        ESP_LOGI(TAG, "Confirmacao desfeita");
        entrar_selecao(agora);
        break;
    case EXECUTANDO:
    case PAUSADO:
        ESP_LOGI(TAG, "Ciclo CANCELADO");
        nvs_ciclo_limpar();
        entrar_selecao(agora);
        buzzer_iniciar_beeps(1, 700, 150, agora);
        break;
    case RECUPERACAO:
        buzzer_parar();
        ESP_LOGI(TAG, "Recuperacao CANCELADA pelo usuario");
        nvs_ciclo_limpar();
        entrar_selecao(agora);
        buzzer_iniciar_beeps(1, 700, 150, agora);
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
    switch (estado)
    {
    case SELECAO:
        on[0] = piscar; // Amarelo sempre pisca na selecao
        if (programa == 1)
            on[1] = true;
        if (programa == 2)
            on[2] = true;
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
        on[2] = piscar; // Vermelho pisca ate escolha do usuario
        break;
    case FINALIZANDO:
        // LEDs acompanham as notas da melodia.
        if (buzzer_emitindo())
        {
            on[0] = on[1] = on[2] = true;
        }
        break;
    case DESLIGADA:
        break;
    }
    leds_exibir(on);
}

void lavadora_inicializar(void)
{
    nvs_ciclo_inicializar();
    RegistroNVS registro_recuperado;
    if (nvs_ciclo_carregar(&registro_recuperado))
    {
        programa = (int)registro_recuperado.programa;
        etapa = (int)registro_recuperado.etapa;
        tempo_etapa_ms = (int64_t)tempos[programa][etapa] * 1000 - registro_recuperado.restante_ms;
        estado = RECUPERACAO;
        ESP_LOGW(TAG, "CICLO INTERROMPIDO DETECTADO!");
        ESP_LOGW(TAG, "Programa: %s | Etapa: %s | restavam %u ms",
                 nomes_programas[programa], nomes_etapas[etapa],
                 (unsigned)registro_recuperado.restante_ms);
        ESP_LOGW(TAG, "Antes do reset: %s",
                 registro_recuperado.estava_pausado ? "PAUSADO" : "EXECUTANDO");
        ESP_LOGW(TAG, "LED vermelho piscando. B1=RETOMAR, B2=CANCELAR");
    }

    if (estado == RECUPERACAO)
    {
        buzzer_iniciar_beeps(2, 160, 160, tempo_ms());
    }
    else
    {
        ESP_LOGI(TAG, "Lavadora pronta! Botao 1 para ligar.");
    }
    ESP_LOGI(TAG, "Programas: rapido 15s, normal 25s, intenso 35s");
    atualizar_leds(tempo_ms());
}

void lavadora_atualizar(int64_t agora)
{
    buzzer_atualizar(agora);
    atualizar_ciclo(agora);

    if (estado == FINALIZANDO && !buzzer_ativo())
    {
        ESP_LOGI(TAG, "Sinalizacao concluida. Retornando a selecao.");
        entrar_selecao(agora);
    }
    atualizar_selecao(agora);

    bool evento_b1 = botao_1_pressionado(agora);
    bool evento_b2 = botao_2_pressionado(agora);
    if (evento_b2)
        tratar_botao_2(agora);
    else if (evento_b1)
        tratar_botao_1(agora);
    atualizar_leds(agora);
}
