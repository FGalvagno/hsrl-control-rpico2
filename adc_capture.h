// adc_capture.h : modulo de captura por adc sincronizado con trigger externo
// captura el valor pico de cada canal durante el pulso del generador de ondas

#ifndef ADC_CAPTURE_H
#define ADC_CAPTURE_H

#include <stdint.h>
#include <stdbool.h>
#include "pico/types.h"

// resultado de un ciclo completo de medicion
// con n=1 son dos pulsos de trigger, con n>1 son 2*n pulsos y los
// campos guardan el promedio de los picos
typedef struct {
    uint16_t pp_raw;    // pico (o promedio de picos) de p+ crudo (12 bits)
    uint16_t pm_raw;    // pico (o promedio de picos) de p- crudo (12 bits)
    float pp;           // idem en volts
    float pm;           // idem en volts
    uint n;             // cuantos picos por canal se promediaron
} medicion_t;

// prepara el adc y el pin de trigger
void adc_capture_init(void);

// espera un pulso en el trigger y devuelve el maximo leido en ese canal
// usa free-running con fifo para maxima velocidad (~500 ksps)
uint16_t adc_capturar_pico(uint canal);

// ciclo completo: espera dos pulsos de trigger, uno para p+ y otro para p-
// equivale a adc_medir_ciclo_n(1)
medicion_t adc_medir_ciclo(void);

// ciclo promediado: repite n veces el par de pulsos (p+ y p-) y devuelve
// el promedio de los n picos de cada canal. n se clampea a [1, N_PROM_MAX]
medicion_t adc_medir_ciclo_n(uint n);

// conversion de valor crudo 12 bits a voltaje (referencia 3.3v)
float adc_a_volts(uint16_t raw);

#endif
