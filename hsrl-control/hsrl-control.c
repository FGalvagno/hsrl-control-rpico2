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
//
// protocolo de comandos (cada linea termina en \n):
//   comandos de modo (un caracter):
//     s  -> stop (detener)
//     f  -> forward (heater sube)
//     b  -> backward (heater baja)
//     g  -> lock automatico con ratio p+/p-
//     e  -> finalizar programa
//     ?  -> mostrar parametros actuales
//   comandos de parametro (letra + valor flotante):
//     P<val> -> piezo voltaje inicial      ej: P2.500
//     V<val> -> piezo paso (step)          ej: V0.010
//     T<val> -> heater setpoint inicial    ej: T62.140
//     N<val> -> heater minimo              ej: N61.300
//     X<val> -> heater maximo              ej: X63.300
//     R<val> -> heater paso grueso         ej: R0.069923
//     M<val> -> heater paso medio          ej: M0.017750
//     I<val> -> heater paso fino           ej: I0.004438

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/sync.h"

#include "config.h"
#include "adc_capture.h"
#include "seeder_comm.h"
#include "control.h"

static ctrl_params_t g_params;
static ctrl_estado_t g_estado;
static critical_section_t g_cs;

static void imprimir_ayuda(void) {
    printf("--- comandos de modo ---\n");
    printf("  s  = detener (stop)\n");
    printf("  f  = avanzar (heater sube)\n");
    printf("  b  = retroceder (heater baja)\n");
    printf("  g  = enganche automatico (lock p+/p-)\n");
    printf("  e  = finalizar programa\n");
    printf("  ?  = mostrar parametros actuales\n");
    printf("--- comandos de parametro (letra + valor + enter) ---\n");
    printf("  P<val> = piezo voltaje inicial    ej: P2.500\n");
    printf("  V<val> = piezo paso (step)        ej: V0.010\n");
    printf("  T<val> = heater setpoint inicial  ej: T62.140\n");
    printf("  N<val> = heater minimo            ej: N61.300\n");
    printf("  X<val> = heater maximo            ej: X63.300\n");
    printf("  R<val> = heater paso grueso       ej: R0.069923\n");
    printf("  M<val> = heater paso medio        ej: M0.017750\n");
    printf("  I<val> = heater paso fino         ej: I0.004438\n");
}


// procesa una linea de comando completa
static void procesar_linea(const char *linea) {
    if (linea[0] == '\0') return;

    char cmd = linea[0];

    // --- comandos de modo (caracter unico) ---
    if (cmd == 's' || cmd == 'f' || cmd == 'b' || cmd == 'g' || cmd == 'e') {
        critical_section_enter_blocking(&g_cs);
        control_comando(cmd, &g_params, &g_estado);
        critical_section_exit(&g_cs);

        switch (cmd) {
            case 's': printf("[ok] sistema detenido\n"); break;
            case 'f': printf("[ok] barriendo hacia adelante (heater subiendo)\n"); break;
            case 'b': printf("[ok] barriendo hacia atras (heater bajando)\n"); break;
            case 'g': printf("[ok] modo lock automatico activado\n"); break;
            case 'e': printf("[ok] finalizando programa\n"); break;
        }
        return;
    }

    if (cmd == '?') {
        // copiar el estado para no mantener el lock durante los printf
        ctrl_params_t snap_p;
        ctrl_estado_t snap_e;
        critical_section_enter_blocking(&g_cs);
        snap_p = g_params;
        snap_e = g_estado;
        critical_section_exit(&g_cs);

        printf("[estado] modo         = %c\n",  snap_e.modo);
        printf("[estado] piezo_v      = %.4f\n", snap_p.piezo_v);
        printf("[estado] piezo_paso   = %.6f\n", snap_p.piezo_paso);
        printf("[estado] heater_sp    = %.4f\n", snap_p.heater_sp);
        printf("[estado] heater_min   = %.4f\n", snap_p.heater_min);
        printf("[estado] heater_max   = %.4f\n", snap_p.heater_max);
        printf("[estado] paso_grueso  = %.6f\n", snap_p.heater_paso_grueso);
        printf("[estado] paso_medio   = %.6f\n", snap_p.heater_paso_medio);
        printf("[estado] paso_fino    = %.6f\n", snap_p.heater_paso_fino);
        printf("[estado] lock_activo  = %s\n",   snap_p.lock_activo ? "si" : "no");
        if (snap_p.lock_activo) {
            printf("[estado] prt_ref      = %.6f\n", snap_p.prt_ref);
        }
        return;
    }

    // --- comandos de parametro (letra + valor flotante) ---
    float val;
    if (sscanf(linea + 1, "%f", &val) != 1) {
        printf("[err] valor invalido: \"%s\"  (formato: letra+numero, ej: T62.14)\n", linea);
        return;
    }

    // aplicar el cambio con el lock tomado el minimo tiempo posible,
    // el echo se imprime despues para no bloquear core0
    bool ok = true;
    critical_section_enter_blocking(&g_cs);
    switch (cmd) {
        case 'P': g_params.piezo_v           = val; break;
        case 'V': g_params.piezo_paso         = val; break;
        case 'T': g_params.heater_sp          = val; break;
        case 'N': g_params.heater_min         = val; break;
        case 'X': g_params.heater_max         = val; break;
        case 'R': g_params.heater_paso_grueso = val; break;
        case 'M': g_params.heater_paso_medio  = val; break;
        case 'I': g_params.heater_paso_fino   = val; break;
        default:  ok = false;                        break;
    }
    critical_section_exit(&g_cs);

    // echo fuera del lock: core0 nunca espera al printf
    if (!ok) {
        printf("[err] comando desconocido: '%c'  (envia ? para ver ayuda)\n", cmd);
        return;
    }
    switch (cmd) {
        case 'P': printf("[ok] piezo voltaje      = %.4f\n",  val); break;
        case 'V': printf("[ok] piezo paso         = %.6f\n",  val); break;
        case 'T': printf("[ok] heater setpoint    = %.4f\n",  val); break;
        case 'N': printf("[ok] heater minimo      = %.4f\n",  val); break;
        case 'X': printf("[ok] heater maximo      = %.4f\n",  val); break;
        case 'R': printf("[ok] heater paso grueso = %.6f\n",  val); break;
        case 'M': printf("[ok] heater paso medio  = %.6f\n",  val); break;
        case 'I': printf("[ok] heater paso fino   = %.6f\n",  val); break;
    }
}

// core1: lee comandos del usuario de forma bloqueante,
// en paralelo con el loop de medicion en core0.
//
// comandos de modo (s/f/b/g/e/?) se procesan al instante, sin esperar \n.
// comandos de parametro (ej: T62.14) se acumulan hasta recibir \n.
static void core1_cmd_task(void) {
    char buf[32];
    int  len = 0;

    while (true) {
        int c = getchar();
        if (c < 0) continue;

        char ch = (char)c;

        if (ch == '\r') continue;   // ignorar CR (terminales windows)

        // comandos de un solo caracter: no necesitan \n
        if (ch == 's' || ch == 'f' || ch == 'b' ||
            ch == 'g' || ch == 'e' || ch == '?') {
            buf[0] = ch;
            buf[1] = '\0';
            len = 0;
            procesar_linea(buf);
            if (ch == 'e') break;
            continue;
        }

        // comando de parametro: acumular hasta \n
        if (ch == '\n') {
            buf[len] = '\0';
            len = 0;
            if (buf[0] != '\0') procesar_linea(buf);
            continue;
        }

        if (len < (int)(sizeof(buf) - 1)) {
            buf[len++] = ch;
        }
    }
}

int main() {
    stdio_init_all();
    // esperar un poco para que la consola usb se conecte
    sleep_ms(6000);
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
