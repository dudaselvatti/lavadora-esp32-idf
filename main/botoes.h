#ifndef BOTOES_LAVADORA_H
#define BOTOES_LAVADORA_H

#include <stdbool.h>
#include <stdint.h>

void botoes_inicializar(void);
bool botao_1_pressionado(int64_t agora_ms);
bool botao_2_pressionado(int64_t agora_ms);

#endif
