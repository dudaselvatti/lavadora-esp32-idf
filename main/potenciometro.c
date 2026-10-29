#include "potenciometro.h"
#include "lavadora_config.h"
#include "esp_err.h"

static adc_oneshot_unit_handle_t adc;

void potenciometro_inicializar(void)
{
    adc_oneshot_unit_init_cfg_t cfg_adc = {.unit_id = ADC_UNIT_1};
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&cfg_adc, &adc));
    adc_oneshot_chan_cfg_t cfg_canal = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12};
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc, ADC_POT, &cfg_canal));
}

int potenciometro_ler(void)
{
    int leitura = 0;
    ESP_ERROR_CHECK(adc_oneshot_read(adc, ADC_POT, &leitura));
    return leitura;
}

int potenciometro_selecionar_programa(int valor, int atual)
{
    if (atual < 0)
    {
        if (valor < 1365)
            return 0;
        if (valor < 2730)
            return 1;
        return 2;
    }
    if (atual == 0)
    {
        if (valor > 2730 + HISTERESE_ADC)
            return 2;
        if (valor > 1365 + HISTERESE_ADC)
            return 1;
    }
    else if (atual == 1)
    {
        if (valor < 1365 - HISTERESE_ADC)
            return 0;
        if (valor > 2730 + HISTERESE_ADC)
            return 2;
    }
    else
    {
        if (valor < 1365 - HISTERESE_ADC)
            return 0;
        if (valor < 2730 - HISTERESE_ADC)
            return 1;
    }
    return atual;
}
