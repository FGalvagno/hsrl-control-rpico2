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
//     a  -> sintonizacion automatica: mientras se barre con f/b se guarda
//           el minimo de p+ y el de p- con su temperatura (y se borran al
//           parar con s). con 'a' va al punto medio entre esas dos
//           temperaturas, espera 20 s, mide 4 ciclos y engancha el lock
//     e  -> finalizar programa
//     ?  -> mostrar parametros actuales (los que tiene el pico)
//     !  -> consultar al seeder temperatura, piezo y flags de second mode
//     d  -> diagnostico: estado del lock + cola de errores del seeder
//     w  -> escribir los setpoints al seeder ahora (no espera al loop)
//     u  -> desbloquear escrituras en el seeder (password, CENable)
//     l  -> volver a bloquearlas (CDISable)
//   comandos de parametro (letra + valor flotante):
//     P<val> -> piezo voltaje inicial      ej: P2.500
//     V<val> -> piezo paso (step)          ej: V0.010
//     T<val> -> heater setpoint inicial    ej: T62.140
//     N<val> -> heater minimo              ej: N61.300
//     X<val> -> heater maximo              ej: X63.300
//     R<val> -> heater paso grueso         ej: R0.069923
//     M<val> -> heater paso medio          ej: M0.017750
//     I<val> -> heater paso fino           ej: I0.004438
//     A<val> -> muestras a promediar (n)   ej: A5
//     E<val> -> espera estabilizacion (s)  ej: E10
//
// sobre el promediado (comando A):
//   con n=1 el ciclo mide un pico de p+ y uno de p- y manda la
//   temperatura al seeder. con n=5 mide 5 picos de p+ y 5 de p-,
//   promedia cada canal y recien ahi calcula el ratio y manda la
//   temperatura. sirve para filtrar el ruido disparo a disparo,
//   a costa de que cada ciclo tarde n veces mas.

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
    printf("  a  = ir al punto medio entre los minimos y enganchar\n");
    printf("  e  = finalizar programa\n");
    printf("  ?  = mostrar parametros actuales (los que tiene el pico)\n");
    printf("  !  = consultar al seeder temperatura, piezo y smod flags\n");
    printf("  d  = diagnostico: estado del lock + cola de errores\n");
    printf("  w  = escribir los setpoints al seeder ahora y verificar\n");
    printf("  u  = desbloquear escrituras en el seeder (password, CENable)\n");
    printf("  l  = volver a bloquearlas (CDISable)\n");
    printf("--- comandos de parametro (letra + valor + enter) ---\n");
    printf("  P<val> = piezo voltaje inicial    ej: P2.500\n");
    printf("  V<val> = piezo paso (step)        ej: V0.010\n");
    printf("  T<val> = heater setpoint inicial  ej: T62.140\n");
    printf("  N<val> = heater minimo            ej: N61.300\n");
    printf("  X<val> = heater maximo            ej: X63.300\n");
    printf("  R<val> = heater paso grueso       ej: R0.069923\n");
    printf("  M<val> = heater paso medio        ej: M0.017750\n");
    printf("  I<val> = heater paso fino         ej: I0.004438\n");
    printf("  A<val> = muestras a promediar     ej: A5   (1..%d)\n", N_PROM_MAX);
    printf("  E<val> = espera estabilizacion    ej: E10  (segundos, 0 = sin pausa)\n");
}


// procesa una linea de comando completa
static void procesar_linea(const char *linea) {
    if (linea[0] == '\0') return;

    char cmd = linea[0];

    // --- comandos de modo (caracter unico) ---
    if (cmd == 's' || cmd == 'f' || cmd == 'b' || cmd == 'g' ||
        cmd == 'e' || cmd == 'a') {
        critical_section_enter_blocking(&g_cs);
        control_comando(cmd, &g_params, &g_estado);
        critical_section_exit(&g_cs);

        switch (cmd) {
            case 'a': printf("[ok] sintonizacion automatica\n"); break;
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
        printf("[estado] n_prom       = %d  (picos por canal antes de mandar temp)\n",
               snap_p.n_prom);
        printf("[estado] espera       = %.1f s  (estabilizacion antes de medir)\n",
               snap_p.espera_ms / 1000.0f);
        printf("[estado] lock_activo  = %s\n",   snap_p.lock_activo ? "si" : "no");
        if (snap_p.lock_activo) {
            printf("[estado] prt_ref      = %.6f\n", snap_p.prt_ref);
        }
        return;
    }

    if (cmd == '!') {
        // consulta directa al seeder por uart. no toca g_params ni g_estado,
        // asi que no hace falta la critical_section; el uart ya esta
        // serializado con su propio mutex adentro de seeder_comm.
        // ojo: son dos transacciones de hasta ~500 ms cada una, este
        // core queda esperando mientras tanto (core0 sigue midiendo).
        printf("[seeder] consultando...\n");
        float t_real = seeder_leer_heater();
        float p_real = seeder_leer_piezo();
        int   smod   = seeder_leer_smod_flags();

        // los setpoints que el pico cree tener, para comparar
        critical_section_enter_blocking(&g_cs);
        float t_sp = g_params.heater_sp;
        float p_sp = g_params.piezo_v;
        critical_section_exit(&g_cs);

        printf("[seeder] temperatura = %.4f  (setpoint %.4f, dif %+.4f)\n",
               t_real, t_sp, t_real - t_sp);
        printf("[seeder] piezo       = %.4f  (setpoint %.4f, dif %+.4f)\n",
               p_real, p_sp, p_real - p_sp);
        if (smod >= 0) {
            printf("[seeder] smod flags  = 0x%02X  -> segundo modo: %s\n",
                   smod, (smod & SMOD_FLAG_SEGUNDO_MODO) ? "SI" : "no");
        } else {
            printf("[seeder] smod flags  = (sin respuesta)\n");
        }
        return;
    }

    if (cmd == 'u' || cmd == 'l') {
        // u = CENable  (manda el password y desbloquea las escrituras)
        // l = CDISable (vuelve a bloquearlas)
        //
        // imprime el estado antes y despues a proposito: el PDF es
        // ambiguo sobre si 1 significa "desbloqueado" o "protegido", y
        // viendo el valor dar vuelta con u/l se resuelve sin adivinar
        int antes = seeder_leer_estado_lock();
        printf("[lock] cen:stat antes    = %d\n", antes);

        if (cmd == 'u') {
            printf("[lock] mandando CENable (desbloquear escrituras)...\n");
            seeder_habilitar_protegido();
        } else {
            printf("[lock] mandando CDISable (volver a bloquear)...\n");
            seeder_bloquear_protegido();
        }

        int despues = seeder_leer_estado_lock();
        printf("[lock] cen:stat despues  = %d\n", despues);

        if (antes < 0 || despues < 0) {
            printf("[lock] el seeder no contesto la query de estado\n");
        } else if (antes == despues) {
            printf("[lock] el estado NO cambio -> el comando no entro,"
                   " revisar la cola de errores con 'd'\n");
        } else {
            printf("[lock] ok, el estado cambio de %d a %d\n", antes, despues);
        }
        return;
    }

    if (cmd == 'w') {
        // escribe los setpoints al seeder AHORA, desde core1, sin pasar
        // por el loop de medicion. sirve para probar la escritura cuando
        // no hay trigger conectado (core0 estaria bloqueado esperandolo)
        ctrl_params_t snap_p;
        critical_section_enter_blocking(&g_cs);
        snap_p = g_params;
        critical_section_exit(&g_cs);

        // valor que tiene el seeder ANTES de escribir
        float sp_antes = seeder_leer_heater_sp();
        printf("[w] setpoint en el seeder antes = %.4f\n", sp_antes);

        printf("[w] escribiendo heater=%.4f  piezo=%.4f ...\n",
               snap_p.heater_sp, snap_p.piezo_v);
        seeder_set_heater(snap_p.heater_sp);
        seeder_set_piezo(snap_p.piezo_v);
        sleep_ms(100);

        // releer el setpoint: cambia apenas el seeder acepta el comando,
        // no hay que esperar a que el heater llegue fisicamente
        float sp_despues = seeder_leer_heater_sp();
        printf("[w] setpoint en el seeder despues = %.4f\n", sp_despues);

        float dif = sp_despues - snap_p.heater_sp;
        if (dif < 0) dif = -dif;

        if (dif < 0.01f) {
            printf("[w] ACEPTADO: el seeder tomo el setpoint\n");
        } else if (sp_despues == sp_antes) {
            printf("[w] RECHAZADO: el setpoint no se movio (mirar la cola con 'd')\n");
        } else {
            printf("[w] raro: quedo en %.4f y se pidio %.4f\n",
                   sp_despues, snap_p.heater_sp);
        }
        return;
    }

    if (cmd == 'd') {
        char resp[32];

        // 1) estado del lock. si esto no da 1, las escrituras de
        //    temperatura y piezo se van a ignorar siempre
        printf("[diag] --- estado de comandos protegidos ---\n");
        int n = seeder_consultar(":syst:pass:cen:stat?\r\n", resp, sizeof(resp));
        if (n <= 0) {
            printf("[diag] cen:stat = (sin respuesta)\n");
        } else {
            printf("[diag] cen:stat = %s   -> %s\n", resp,
                   (resp[0] == '1') ? "DESBLOQUEADO" : "PROTEGIDO (las escrituras se ignoran)");
        }

        // 2) vaciar la cola de errores. es fifo y guarda hasta 10,
        //    el codigo 0 significa que ya no queda nada
        printf("[diag] --- cola de errores ---\n");
        for (int k = 0; k < 10; k++) {
            char desc[48];
            int cod = seeder_leer_error(desc, sizeof(desc));

            if (cod == SEEDER_ERR_SIN_RESPUESTA) {
                printf("[diag] (sin respuesta al leer la cola)\n");
                break;
            }
            if (cod == 0) {
                printf("[diag] sin errores pendientes\n");
                break;
            }
            printf("[diag] %+d  %s\n", cod, desc);
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
    int  n_aplicado = 0;         // solo lo usa el comando A, para el echo
    uint32_t espera_aplicada = 0;   // idem para el comando E
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
        case 'A':
            // n es entero: se redondea y se clampea al rango valido
            n_aplicado = (val > 0.0f) ? (int)(val + 0.5f) : 1;
            if (n_aplicado < 1)          n_aplicado = 1;
            if (n_aplicado > N_PROM_MAX) n_aplicado = N_PROM_MAX;
            g_params.n_prom = n_aplicado;
            break;
        case 'E':
            // se ingresa en segundos y se guarda en ms
            if (val < 0.0f) val = 0.0f;
            espera_aplicada = (uint32_t)(val * 1000.0f + 0.5f);
            if (espera_aplicada > ESPERA_MAX_MS) espera_aplicada = ESPERA_MAX_MS;
            g_params.espera_ms = espera_aplicada;
            break;
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
        case 'A': printf("[ok] muestras a promediar = %d por canal (%d triggers por ciclo)\n",
                         n_aplicado, 2 * n_aplicado); break;
        case 'E': printf("[ok] espera de estabilizacion = %.1f s%s\n",
                         espera_aplicada / 1000.0f,
                         (espera_aplicada == 0) ? "  (sin pausa)" : ""); break;
    }
}

// --- sintonizacion automatica (modo a) ---
// las cuatro variables: minimo de cada canal y la temperatura donde ocurrio
static float pp_min, t_pp_min, pm_min, t_pm_min;

// se llama una vez por ciclo, antes de control_actualizar (que aplica el
// delta): asi la medicion queda asociada al setpoint con el que se tomo
static void auto_actualizar(char modo, float t, float pp, float pm) {
    if (modo == MODO_STOP) {
        pp_min = t_pp_min = pm_min = t_pm_min = 0.0f;
        return;
    }
    if (modo != MODO_FORWARD && modo != MODO_BACKWARD) return;

    // el == 0 cubre el arranque despues de un stop: la primera medicion
    // entra siempre, sin necesidad de un flag aparte
    if (pp_min == 0.0f || pp < pp_min) { pp_min = pp; t_pp_min = t; }
    if (pm_min == 0.0f || pm < pm_min) { pm_min = pm; t_pm_min = t; }
}

static void auto_ejecutar(ctrl_params_t *p, ctrl_estado_t *e) {
    // sin barrido previo las cuatro variables estan en cero y el punto
    // medio daria 0.0, que el seeder rechaza con -222 data out of range
    if (t_pp_min == 0.0f || t_pm_min == 0.0f) {
        printf("[auto] no hay minimos registrados, barrer con f o b primero\n");
        e->modo = MODO_STOP;
        return;
    }

    p->heater_sp = (t_pp_min + t_pm_min) / 2.0f;
    printf("[auto] min p+ en %.4f | min p- en %.4f | setpoint %.4f\n",
           t_pp_min, t_pm_min, p->heater_sp);

    seeder_set_heater(p->heater_sp);
    sleep_ms(20000);

    // 4 ciclos en stop: miden y llenan hist[] sin mover el heater
    e->modo = MODO_STOP;
    for (int i = 0; i < 4; i++) {
        medicion_t m = adc_medir_ciclo_n((uint)p->n_prom);
        control_actualizar(m.pp, m.pm, p, e);
    }

    // el lock toma prt_ref de hist[2], que ahora ya esta estabilizado
    e->modo = MODO_LOCK;
    printf("[auto] enganchado en %.4f\n", p->heater_sp);
}

// core1: lee comandos del usuario de forma bloqueante,
// en paralelo con el loop de medicion en core0.
//
// comandos de modo (s/f/b/g/e/?/!) se procesan al instante, sin esperar \n.
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
            ch == 'g' || ch == 'e' || ch == '?' || ch == '!' ||
            ch == 'd' || ch == 'w' || ch == 'u' || ch == 'l' ||
            ch == 'a') {
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

// espera parpadeando el led. se usa entre reintentos de desbloqueo,
// donde es la unica señal de vida disponible porque la consola usb
// todavia no esta conectada. deja el led apagado al salir
static void esperar_parpadeando(uint32_t total_ms, uint32_t medio_periodo_ms) {
    uint32_t transcurrido = 0;
    bool encendido = false;

    while (transcurrido < total_ms) {
        encendido = !encendido;
        gpio_put(PICO_DEFAULT_LED_PIN, encendido);

        // el ultimo tramo puede ser mas corto, para no pasarse del total
        uint32_t restante = total_ms - transcurrido;
        uint32_t paso = (medio_periodo_ms < restante) ? medio_periodo_ms : restante;

        sleep_ms(paso);
        transcurrido += paso;
    }

    gpio_put(PICO_DEFAULT_LED_PIN, 0);
}

int main() {
    stdio_init_all();

    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    gpio_put(PICO_DEFAULT_LED_PIN, 0);

    // --- desbloqueo del seeder, antes que nada ---
    // se hace aca y no despues de la espera de usb para que el seeder ya
    // acepte escrituras cuando arranque el loop. el uart tiene que estar
    // inicializado si o si antes de poder hablarle
    seeder_init();

    // reintentar el password hasta que enganche. los printf de estos
    // intentos se pierden porque la consola usb todavia no esta
    // conectada, asi que el resultado se guarda y se informa mas abajo
    int  intentos_unlock = 0;
    bool seeder_ok = false;
    // el led acompaña: fijo mientras se manda el intento, parpadeando
    // rapido durante la espera. asi se distingue "trabajando" de "colgado"
    while (intentos_unlock < UNLOCK_MAX_INTENTOS && !seeder_ok) {
        intentos_unlock++;

        gpio_put(PICO_DEFAULT_LED_PIN, 1);
        seeder_ok = seeder_habilitar_protegido();
        gpio_put(PICO_DEFAULT_LED_PIN, 0);

        // no esperar despues del ultimo intento, no sirve de nada
        if (!seeder_ok && intentos_unlock < UNLOCK_MAX_INTENTOS) {
            esperar_parpadeando(UNLOCK_REINTENTO_MS, UNLOCK_PARPADEO_MS);
        }
    }

    // esperar un poco para que la consola usb se conecte
    sleep_ms(6000);
    gpio_put(PICO_DEFAULT_LED_PIN, 1);

    printf("=== hsrl-control v0.2 ===\n");
    printf("placa: raspberry pi pico 2\n");
    printf("trigger: gpio%d | adc p+: gpio%d | adc p-: gpio%d\n",
           PIN_TRIGGER, PIN_ADC_PP, PIN_ADC_PM);
    printf("seeder tx: gpio%d | seeder rx: gpio%d | baud: %d\n\n",
           PIN_SEEDER_TX, PIN_SEEDER_RX, SEEDER_BAUD);
    imprimir_ayuda();
    printf("\n");

    // inicializar los modulos (el seeder ya se inicializo mas arriba)
    adc_capture_init();

    // recien ahora se puede informar como salio el desbloqueo
    if (seeder_ok) {
        printf("seeder desbloqueado (intento %d de %d)\n",
               intentos_unlock, UNLOCK_MAX_INTENTOS);
    } else {
        printf("aviso: no se pudo desbloquear el seeder en %d intentos\n",
               UNLOCK_MAX_INTENTOS);
        printf("       las escrituras de temperatura y piezo se van a ignorar\n");
        printf("       probar 'u' para reintentar y 'd' para ver el motivo\n");
    }

    // preparar el control y lanzar core1 para manejar comandos
    critical_section_init(&g_cs);
    control_init(&g_params, &g_estado);
    multicore_launch_core1(core1_cmd_task);

    printf("heater sp inicial: %.4f | piezo: %.3f\n", g_params.heater_sp, g_params.piezo_v);
    printf("limites heater: [%.2f, %.2f]\n", g_params.heater_min, g_params.heater_max);
    printf("promediado: n=%d picos por canal (comando A para cambiarlo)\n", g_params.n_prom);

    // contamos los ciclos para referencia
    int ciclo = 0;

    while (control_activo(&g_estado)) {

        if (g_estado.modo == MODO_AUTO) {
            auto_ejecutar(&g_params, &g_estado);
            continue;
        }

        printf("[%d] \n", ciclo);

        // leer n con el lock tomado: core1 lo puede cambiar en cualquier
        // momento con el comando A, y no queremos que cambie a mitad del
        // promedio. la copia local rige para todo este ciclo
        critical_section_enter_blocking(&g_cs);
        int      n_prom   = g_params.n_prom;
        uint32_t espera   = g_params.espera_ms;
        critical_section_exit(&g_cs);

        // dar tiempo a que la temperatura del heater se estabilice antes
        // de medir: el setpoint se mando al final del ciclo anterior y el
        // heater tarda en llegar. core1 sigue atendiendo comandos durante
        // la pausa, asi que se puede cambiar con E sin esperar a que corte
        if (espera > 0) {
            printf("esperando estabilizacion del seeder (%.1f s)...\n",
                   espera / 1000.0f);
            sleep_ms(espera);
        }

        // medir: n pares de pulsos de trigger (uno para p+, otro para p-),
        // promediando los picos de cada canal.
        // durante este bloqueo, core1 sigue atendiendo comandos del usuario
        printf("esperando %d trigger(s) en gpio%d (n=%d por canal)...\n",
               2 * n_prom, PIN_TRIGGER, n_prom);
        medicion_t med = adc_medir_ciclo_n((uint)n_prom);

        // actualizar la logica de control con las mediciones nuevas
        printf("[%d] medicion (prom de %u): pp=%.6f pm=%.6f\n",
               ciclo, med.n, med.pp, med.pm);
        critical_section_enter_blocking(&g_cs);
        auto_actualizar(g_estado.modo, g_params.heater_sp, med.pp, med.pm);
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

        // cierre de ciclo: flags de second mode. si el bit 0 esta en 1 el
        // seeder salio de modo unico y la medicion de este ciclo no es
        // confiable. wvcnt_lec.cpp consulta aca el nivel crudo; usamos los
        // flags porque el seeder ya lo compara contra su propio umbral
        int smod = seeder_leer_smod_flags();
        if (smod < 0) {
            printf("[%d] smod flags = (sin respuesta)\n", ciclo);
        } else if (smod & SMOD_FLAG_SEGUNDO_MODO) {
            printf("[%d] smod flags = 0x%02X  *** SEGUNDO MODO DETECTADO ***\n",
                   ciclo, smod);
        } else {
            printf("[%d] smod flags = 0x%02X  (modo unico ok)\n", ciclo, smod);
        }

        ciclo++;
    }

    // dejar el heater en el ultimo valor y terminar limpio
    printf("programa terminado (%d ciclos)\n", ciclo);
    return 0;
}
