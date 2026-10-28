#ifndef NVS_CICLO_LAVADORA_H
#define NVS_CICLO_LAVADORA_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t assinatura;
    uint32_t versao;
    uint32_t programa;
    uint32_t etapa;
    uint32_t restante_ms;
    uint32_t estava_pausado;
} RegistroNVS;

void nvs_ciclo_inicializar(void);
bool nvs_ciclo_carregar(RegistroNVS *registro);
bool nvs_ciclo_salvar(int programa, int etapa, int64_t tempo_etapa_ms, bool pausado);
bool nvs_ciclo_limpar(void);

#endif
