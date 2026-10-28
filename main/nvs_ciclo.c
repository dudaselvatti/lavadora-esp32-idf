#include <stddef.h>
#include "nvs_ciclo.h"
#include "lavadora_config.h"
#include "programas.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "LAVADORA";
static nvs_handle_t handle_nvs_lavadora;

bool nvs_ciclo_salvar(int programa, int etapa, int64_t tempo_etapa_ms, bool pausado)
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

    ESP_LOGI(TAG, "NVS salvo: %s | %s | restam %u ms | %s",
             nomes_programas[programa], nomes_etapas[etapa],
             (unsigned)registro.restante_ms,
             pausado ? "PAUSADO" : "EXECUTANDO");
    return true;
}

bool nvs_ciclo_limpar(void)
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



void nvs_ciclo_inicializar(void)
{
    // Nao apaga a NVS automaticamente ao ocorrer erro.
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(nvs_open("lavadora", NVS_READWRITE, &handle_nvs_lavadora));
}

bool nvs_ciclo_carregar(RegistroNVS *registro)
{
    size_t tamanho = 0;
    esp_err_t erro = nvs_get_blob(handle_nvs_lavadora, "ciclo", NULL, &tamanho);
    if (erro == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "NVS: nenhum ciclo interrompido encontrado");
        return false;
    }
    ESP_ERROR_CHECK(erro);

    if (tamanho != sizeof(*registro)) {
        ESP_LOGW(TAG, "NVS: registro antigo/incompativel; descartando");
        ESP_ERROR_CHECK(nvs_ciclo_limpar() ? ESP_OK : ESP_FAIL);
        return false;
    }
    ESP_ERROR_CHECK(nvs_get_blob(handle_nvs_lavadora, "ciclo", registro, &tamanho));
    if (!registro_valido(registro)) {
        ESP_LOGW(TAG, "NVS: registro invalido; descartando");
        ESP_ERROR_CHECK(nvs_ciclo_limpar() ? ESP_OK : ESP_FAIL);
        return false;
    }
    return true;
}
