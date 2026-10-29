#ifndef PROGRAMAS_LAVADORA_H
#define PROGRAMAS_LAVADORA_H

#include <stdint.h>

#define TOTAL_PROGRAMAS 3
#define TOTAL_ETAPAS   3

extern const char *const nomes_programas[TOTAL_PROGRAMAS];
extern const char *const nomes_etapas[TOTAL_ETAPAS];
extern const uint8_t tempos[TOTAL_PROGRAMAS][TOTAL_ETAPAS];
extern const uint8_t totais[TOTAL_PROGRAMAS];

#endif
