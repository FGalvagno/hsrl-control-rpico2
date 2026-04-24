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
#include "pico/multicore.h"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "blink.pio.h"

#include "config.h"
#include "adc_capture.h"
#include "seeder_comm.h"
#include "control.h"

static ctrl_params_t g_params;
static ctrl_estado_t g_estado;
static critical_section_t g_cs;

// pone a parpadear el led con el programa pio
static void led_init(PIO pio, uint sm, uint offset, uint pin, uint freq, bool enable) {
    blink_program_init(pio, sm, offset, pin);
    pio_sm_set_enabled(pio, sm, enable);
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

// core1: espera comandos del usuario de forma bloqueante,
// en paralelo con el loop de medicion en core0
static void core1_cmd_task(void) {
    while (true) {
        int c = getchar();
        if (c < 0) continue;
        char ch = (char)c;

        critical_section_enter_blocking(&g_cs);
        control_comando(ch, &g_params, &g_estado);
        critical_section_exit(&g_cs);

        if (ch == 'e') {
            printf("finalizando...\n");
            break;
        }
        if (ch == 's' || ch == 'f' || ch == 'b' || ch == 'g') {
            printf("[cmd] modo -> %c\n", ch);
        }
    }
}

int main() {
    // led de estado: parpadea a 3 hz mientras el programa corre
    PIO pio = pio0;
    uint offset = pio_add_program(pio, &blink_program);
    led_init(pio, 0, offset, PICO_DEFAULT_LED_PIN, 10, true);

    stdio_init_all();
    // esperar un poco para que la consola usb se conecte
    sleep_ms(6000);
    led_init(pio, 0, offset, PICO_DEFAULT_LED_PIN, 3, true);
    printf("=== hsrl-control v0.2 ===\n");
    printf("placa: raspberry pi pico 2\n");
    printf("trigger: gpio%d | adc p+: gpio%d | adc p-: gpio%d\n",
           PIN_TRIGGER, PIN_ADC_PP, PIN_ADC_PM);
    printf("seeder tx: gpio%d | seeder rx: gpio%d | baud: %d\n\n",
           PIN_SEEDER_TX, PIN_SEEDER_RX, SEEDER_BAUD);
    imprimir_ayuda();
    printf("\n");

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

    // preparar el control y lanzar core1 para manejar comandos
    critical_section_init(&g_cs);
    control_init(&g_params, &g_estado);
    multicore_launch_core1(core1_cmd_task);

    printf("heater sp inicial: %.4f | piezo: %.3f\n", g_params.heater_sp, g_params.piezo_v);
    printf("limites heater: [%.2f, %.2f]\n", g_params.heater_min, g_params.heater_max);

    // contamos los ciclos para referencia
    int ciclo = 0;

    while (control_activo(&g_estado)) {
        printf("[%d] \n", ciclo);

        // medir: espera dos pulsos de trigger (uno para p+, otro para p-)
        // durante este bloqueo, core1 sigue atendiendo comandos del usuario
        printf("esperando trigger en gpio%d...\n", PIN_TRIGGER);
        medicion_t med = adc_medir_ciclo();

        // actualizar la logica de control con las mediciones nuevas
        printf("[%d] medicion: pp=%.6f pm=%.6f\n", ciclo, med.pp, med.pm);
        critical_section_enter_blocking(&g_cs);
        control_actualizar(med.pp, med.pm, &g_params, &g_estado);
        critical_section_exit(&g_cs);

        // mandar los setpoints al seeder por serial
        printf("[%d] aplicando control: d_heater=%+.6f\n", ciclo, g_estado.d_heater);
        seeder_set_piezo(g_params.piezo_v);
        seeder_set_heater(g_params.heater_sp);

        // leer los valores que el seeder reporta
        printf("[%d] leyendo seeder...\n", ciclo);
        float piezo_real = seeder_leer_piezo();
        float heater_real = seeder_leer_heater();

        // mostrar estado por consola usb
        printf("[%d] modo=%c | heater_sp=%.4f heater_real=%.4f | piezo=%.3f piezo_real=%.4f\n",
               ciclo, g_estado.modo, g_params.heater_sp, heater_real,
               g_params.piezo_v, piezo_real);
        printf("     pp=%.6f pm=%.6f prt=%.6f | d_heater=%+.6f\n",
               med.pp, med.pm, g_estado.prt, g_estado.d_heater);

        if (g_estado.modo == MODO_LOCK && g_params.lock_activo) {
            printf("     lock: ref=%.6f banda=[%.6f, %.6f]\n",
                   g_params.prt_ref,
                   g_params.prt_ref / g_params.coe,
                   g_params.prt_ref * g_params.coe);
        }

        ciclo++;
    }

    // dejar el heater en el ultimo valor y terminar limpio
    printf("programa terminado (%d ciclos)\n", ciclo);
    return 0;
}
