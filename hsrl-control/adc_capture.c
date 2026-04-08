// adc_capture.c : implementacion de la captura por adc con trigger externo
// la idea es muestrear lo mas rapido posible mientras el trigger esta alto
// y quedarse con el valor maximo de ese intervalo

#include "adc_capture.h"
#include "config.h"
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"

// referencia del adc interno del rp2350
#define ADC_VREF 3.3f
#define ADC_MAX_VAL 4095.0f

void adc_capture_init(void) {
    adc_init();

    // habilitar gpio26 y gpio27 como entradas analogicas
    adc_gpio_init(PIN_ADC_PP);
    adc_gpio_init(PIN_ADC_PM);

    // pin de trigger como entrada digital con pull-down
    gpio_init(PIN_TRIGGER);
    gpio_set_dir(PIN_TRIGGER, GPIO_IN);
    gpio_pull_down(PIN_TRIGGER);

    // sin divisor de clock para conversion a maxima velocidad
    adc_set_clkdiv(0);
}

uint16_t adc_capturar_pico(uint canal) {
    uint16_t maximo = 0;

    adc_select_input(canal);

    // quedarse aca hasta que el trigger suba
    while (!gpio_get(PIN_TRIGGER)) {
        tight_loop_contents();
    }

    // arrancar free-running con fifo habilitado
    // parametros: fifo_en, dreq_en, dreq_thresh, err_in_fifo, byte_shift
    adc_fifo_setup(true, false, 1, false, false);
    adc_run(true);

    // mientras el trigger este alto, sacar muestras del fifo
    // y quedarse con la mas grande
    while (gpio_get(PIN_TRIGGER)) {
        if (!adc_fifo_is_empty()) {
            uint16_t val = adc_fifo_get();
            if (val > maximo) {
                maximo = val;
            }
        }
    }

    // parar las conversiones
    adc_run(false);

    // vaciar lo que haya quedado en el fifo
    while (!adc_fifo_is_empty()) {
        uint16_t val = adc_fifo_get();
        if (val > maximo) {
            maximo = val;
        }
    }
    adc_fifo_drain();

    return maximo;
}

medicion_t adc_medir_ciclo(void) {
    medicion_t m;

    // primer pulso: canal p+
    m.pp_raw = adc_capturar_pico(ADC_CANAL_PP);
    m.pp = adc_a_volts(m.pp_raw);

    // segundo pulso: canal p-
    m.pm_raw = adc_capturar_pico(ADC_CANAL_PM);
    m.pm = adc_a_volts(m.pm_raw);

    return m;
}

float adc_a_volts(uint16_t raw) {
    return (float)raw * ADC_VREF / ADC_MAX_VAL;
}
