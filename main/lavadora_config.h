#ifndef CONFIG_LAVADORA_H
#define CONFIG_LAVADORA_H

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"

// Pinagem validada na PCB fisica.
#define LED_AMARELO    GPIO_NUM_18
#define LED_VERDE      GPIO_NUM_17
#define LED_VERMELHO   GPIO_NUM_16
#define BOTAO_1        GPIO_NUM_32
#define BOTAO_2        GPIO_NUM_33
#define BUZZER         GPIO_NUM_25
#define ADC_POT        ADC_CHANNEL_6 // GPIO34 / ADC1

// 1: PWM (melodia); 0: acionamento digital.
#define BUZZER_USAR_PWM 1
#define DEBOUNCE_MS     50
#define PISCAR_MS       350
#define HISTERESE_ADC   50
#define CHECKPOINT_MS   2000
#define NVS_ASSINATURA  0x4C415641UL  // "LAVA"
#define NVS_VERSAO      1

#endif
