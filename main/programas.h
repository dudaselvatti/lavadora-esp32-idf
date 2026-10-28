#ifndef PROGRAMAS_LAVADORA_H
#define PROGRAMAS_LAVADORA_H

#define TOTAL_PROGRAMAS 3
#define TOTAL_ETAPAS   3

extern const char *const nomes_programas[TOTAL_PROGRAMAS];
extern const char *const nomes_etapas[TOTAL_ETAPAS];
extern const int tempos[TOTAL_PROGRAMAS][TOTAL_ETAPAS];
extern const int totais[TOTAL_PROGRAMAS];

#endif
