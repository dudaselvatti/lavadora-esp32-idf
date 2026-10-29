#include "programas.h"

const char *const nomes_programas[3] = {"RAPIDO", "NORMAL", "INTENSO"};
const char *const nomes_etapas[3] = {"LAVAGEM", "ENXAGUE", "CENTRIFUGACAO"};
const uint8_t tempos[TOTAL_PROGRAMAS][TOTAL_ETAPAS] = {
    {7, 4, 4},
    {12, 7, 6},
    {18, 9, 8}
};

const uint8_t totais[TOTAL_PROGRAMAS] = {15, 25, 35};
