#ifndef POTENCIOMETRO_LAVADORA_H
#define POTENCIOMETRO_LAVADORA_H

void potenciometro_inicializar(void);
int potenciometro_ler(void);
int potenciometro_selecionar_programa(int valor_adc, int programa_atual);

#endif
