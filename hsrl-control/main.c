// main.c : programa principal de control de longitud de onda para hsrl
// corre en raspberry pi pico 2 (rp2350)
//
// reemplaza al sistema anterior que usaba un osciloscopio tektronix
// conectado por usb-visa para medir las señales del detector.
// ahora el pico mide directamente p+ y p- con su adc, sincronizado
// con el trigger del generador de ondas.
//
// la comunicacion con el seeder continuum es por uart (serial),
// y el usuario controla todo desde una consola usb.

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "blink.pio.h"

#include "config.h"
#include "adc_capture.h"
#include "seeder_comm.h"
#include "control.h"

// pone a parpadear el led con el programa pio
static void led_init(PIO pio, uint sm, uint offset, uint pin, uint freq) {
    blink_program_init(pio, sm, offset, pin);
    pio_sm_set_enabled(pio, sm, true);
    pio->txf[sm] = (125000000 / (2 * freq)) - 3;
}

static void imprimir_ayuda(void) {
    printf("comandos:\n");
    printf("  s = detener (stop)\n");
    printf("  f = avanzar (sube temperatura heater)\n");
    printf("  b = retroceder (baja temperatura heater)\n");
    printf("  g = enganche automatico (lock con ratio p+/p-)\n");
    printf("  e = finalizar programa\n");
}

int main() {
    stdio_init_all();

    // esperar un poco para que la consola usb se conecte
    sleep_ms(2000);

    printf("=== hsrl-control v0.2 ===\n");
    printf("placa: raspberry pi pico 2\n");
    printf("trigger: gpio%d | adc p+: gpio%d | adc p-: gpio%d\n",
           PIN_TRIGGER, PIN_ADC_PP, PIN_ADC_PM);
    printf("seeder tx: gpio%d | seeder rx: gpio%d | baud: %d\n\n",
           PIN_SEEDER_TX, PIN_SEEDER_RX, SEEDER_BAUD);
    imprimir_ayuda();
    printf("\n");

    // led de estado: parpadea a 3 hz mientras el programa corre
    PIO pio = pio0;
    uint offset = pio_add_program(pio, &blink_program);
#ifdef PICO_DEFAULT_LED_PIN
    led_init(pio, 0, offset, PICO_DEFAULT_LED_PIN, 3);
#endif

    // inicializar los modulos
    adc_capture_init();
    seeder_init();

    printf("conectando con seeder...\n");
    bool seeder_ok = seeder_habilitar_protegido();
    if (seeder_ok) {
        printf("seeder listo (comandos protegidos habilitados)\n");
    } else {
        printf("aviso: no se pudo verificar el seeder, revisar conexion\n");
    }

    // preparar el control
    ctrl_params_t params;
    ctrl_estado_t estado;
    control_init(&params, &estado);

    printf("heater sp inicial: %.4f | piezo: %.3f\n", params.heater_sp, params.piezo_v);
    printf("limites heater: [%.2f, %.2f]\n", params.heater_min, params.heater_max);
    printf("esperando trigger en gpio%d...\n\n", PIN_TRIGGER);

    // contamos los ciclos para referencia
    int ciclo = 0;

    while (control_activo(&estado)) {

        // leer comando del usuario si hay algo en la consola usb
        int c = getchar_timeout_us(0);
        if (c != PICO_ERROR_TIMEOUT) {
            char ch = (char)c;
            control_comando(ch, &params, &estado);

            if (ch == 'e') {
                printf("finalizando...\n");
                break;
            }
            if (ch == 's' || ch == 'f' || ch == 'b' || ch == 'g') {
                printf("[%d] modo -> %c\n", ciclo, ch);
            }
        }

        // medir: espera dos pulsos de trigger (uno para p+, otro para p-)
        medicion_t med = adc_medir_ciclo();

        // actualizar la logica de control con las mediciones nuevas
        control_actualizar(med.pp, med.pm, &params, &estado);

        // mandar los setpoints al seeder por serial
        seeder_set_piezo(params.piezo_v);
        seeder_set_heater(params.heater_sp);

        // leer los valores que el seeder reporta
        float piezo_real = seeder_leer_piezo();
        float heater_real = seeder_leer_heater();

        // mostrar estado por consola usb
        printf("[%d] modo=%c | heater_sp=%.4f heater_real=%.4f | piezo=%.3f piezo_real=%.4f\n",
               ciclo, estado.modo, params.heater_sp, heater_real,
               params.piezo_v, piezo_real);
        printf("     pp=%.6f pm=%.6f prt=%.6f | d_heater=%+.6f\n",
               med.pp, med.pm, estado.prt, estado.d_heater);

        if (estado.modo == MODO_LOCK && params.lock_activo) {
            printf("     lock: ref=%.6f banda=[%.6f, %.6f]\n",
                   params.prt_ref,
                   params.prt_ref / params.coe,
                   params.prt_ref * params.coe);
        }

        ciclo++;
    }

    // dejar el heater en el ultimo valor y terminar limpio
    printf("programa terminado (%d ciclos)\n", ciclo);
    return 0;
}
