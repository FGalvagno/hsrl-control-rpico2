// adc_capture.h : modulo de captura por adc sincronizado con trigger externo
// captura el valor pico de cada canal durante el pulso del generador de ondas

#ifndef ADC_CAPTURE_H
#define ADC_CAPTURE_H

#include <stdint.h>
#include <stdbool.h>
#include "pico/types.h"

// resultado de un ciclo completo de medicion (dos pulsos de trigger)
typedef struct {
    uint16_t pp_raw;    // pico de p+ en valor crudo (12 bits)
    uint16_t pm_raw;    // pico de p- en valor crudo (12 bits)
    float pp;           // pico de p+ en volts
    float pm;           // pico de p- en volts
} medicion_t;

// prepara el adc y el pin de trigger
void adc_capture_init(void);

// espera un pulso en el trigger y devuelve el maximo leido en ese canal
// usa free-running con fifo para maxima velocidad (~500 ksps)
uint16_t adc_capturar_pico(uint canal);

// ciclo completo: espera dos pulsos de trigger, uno para p+ y otro para p-
medicion_t adc_medir_ciclo(void);

// conversion de valor crudo 12 bits a voltaje (referencia 3.3v)
float adc_a_volts(uint16_t raw);

#endif
