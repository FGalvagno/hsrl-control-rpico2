// adc_capture.c : implementacion de la captura por adc con trigger externo
// la idea es muestrear lo mas rapido posible mientras el trigger esta alto
// y quedarse con el valor maximo de ese intervalo para tomar decisiones

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

    // mientras el trigger del laser este alto, sacar muestras del fifo
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

// version en punto flotante de adc_a_volts, para el promedio
static float raw_f_a_volts(float raw) {
    return raw * ADC_VREF / ADC_MAX_VAL;
}

medicion_t adc_medir_ciclo_n(uint n) {
    medicion_t m;

    // clamp de seguridad: n=0 no tiene sentido y un n enorme
    // dejaria el ciclo esperando triggers por demasiado tiempo
    if (n < 1) n = 1;
    if (n > N_PROM_MAX) n = N_PROM_MAX;

    // acumuladores de 32 bits: n picos de 12 bits nunca desbordan
    // (N_PROM_MAX * 4095 queda muy por debajo de 2^32)
    uint32_t acum_pp = 0;
    uint32_t acum_pm = 0;

    // se alterna p+ / p- en cada par de pulsos, igual que antes,
    // asi las dos señales se muestrean intercaladas en el tiempo
    for (uint i = 0; i < n; i++) {
        acum_pp += adc_capturar_pico(ADC_CANAL_PP);   // pulso impar
        acum_pm += adc_capturar_pico(ADC_CANAL_PM);   // pulso par
    }

    // el promedio se calcula en float para no perder resolucion:
    // con n grande la parte fraccionaria vale mas que 1 lsb
    float prom_pp = (float)acum_pp / (float)n;
    float prom_pm = (float)acum_pm / (float)n;

    m.pp = raw_f_a_volts(prom_pp);
    m.pm = raw_f_a_volts(prom_pm);

    // los campos crudos quedan redondeados al entero mas cercano,
    // son solo informativos
    m.pp_raw = (uint16_t)(prom_pp + 0.5f);
    m.pm_raw = (uint16_t)(prom_pm + 0.5f);
    m.n = n;

    return m;
}

medicion_t adc_medir_ciclo(void) {
    return adc_medir_ciclo_n(1);
}

float adc_a_volts(uint16_t raw) {
    return (float)raw * ADC_VREF / ADC_MAX_VAL;
}
