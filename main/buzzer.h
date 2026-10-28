#ifndef BUZZER_LAVADORA_H
#define BUZZER_LAVADORA_H

#include <stdbool.h>
#include <stdint.h>

void buzzer_inicializar(void);
void buzzer_parar(void);
void buzzer_iniciar_beeps(int quantidade, int duracao_ms, int intervalo_ms, int64_t agora_ms);
void buzzer_iniciar_melodia_final(int64_t agora_ms);
void buzzer_atualizar(int64_t agora_ms);
bool buzzer_ativo(void);
bool buzzer_emitindo(void);

#endif
