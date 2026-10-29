#ifndef NVS_CICLO_LAVADORA_H
#define NVS_CICLO_LAVADORA_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t assinatura;
    uint16_t restante_ms;
    uint8_t versao;
    uint8_t programa;
    uint8_t etapa;
    uint8_t estava_pausado;
} RegistroNVS;

_Static_assert(sizeof(RegistroNVS) == 12,
               "Tamanho inesperado do registro NVS");

void nvs_ciclo_inicializar(void);
bool nvs_ciclo_carregar(RegistroNVS *registro);
bool nvs_ciclo_salvar(int programa, int etapa, int64_t tempo_etapa_ms, bool pausado);
bool nvs_ciclo_limpar(void);

#endif
