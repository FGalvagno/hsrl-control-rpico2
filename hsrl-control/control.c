// control.c : implementacion de la logica de control de longitud de onda
// basado en el esquema de wvcnt_lec.cpp:
//   - modo stop (s): no hace nada
//   - modo forward (f): sube heater en pasos gruesos
//   - modo backward (b): baja heater en pasos gruesos
//   - modo lock (g): ajusta automaticamente con el ratio p+/p-
//   - modo end (e): termina
//
// la longitud de onda del seeder sube cuando sube la temperatura del heater
// el lock mantiene el ratio p+/p- dentro de una banda alrededor de la referencia

#include "control.h"
#include "config.h"
#include <string.h>

void control_init(ctrl_params_t *p, ctrl_estado_t *e) {
    // piezo fijo
    p->piezo_v = DEFAULT_PIEZO_V;
    p->piezo_paso = 0.0f;

    // heater
    p->heater_sp = DEFAULT_HEATER_SP;
    p->heater_paso_grueso = HEATER_PASO_GRUESO;
    p->heater_paso_medio = HEATER_PASO_MEDIO;
    p->heater_paso_fino = HEATER_PASO_FINO;
    p->heater_min = HEATER_MIN;
    p->heater_max = HEATER_MAX;

    // lock
    p->coe = LOCK_COE;
    p->prt_ref = 0.0f;
    p->lock_activo = false;

    // estado en cero
    memset(e, 0, sizeof(*e));
    e->modo = MODO_STOP;
}

void control_comando(char c, ctrl_params_t *p, ctrl_estado_t *e) {
    // solo aceptar caracteres validos como modo
    if (c == 's' || c == 'f' || c == 'b' || c == 'g' || c == 'e') {
        e->modo = (modo_t)c;
    }

    // al salir del lock, resetear para que la proxima vez
    // tome una nueva referencia
    if (c != 'g') {
        p->lock_activo = false;
    }
}

void control_actualizar(float pp, float pm, ctrl_params_t *p, ctrl_estado_t *e) {
    // rotar historial: [1] -> [2], [0] -> [1]
    e->hist[2] = e->hist[1];
    e->hist[1] = e->hist[0];

    // guardar la medicion actual
    e->hist[0].pp = pp;
    e->hist[0].pm = pm;

    // calcular ratio, cuidando la division por cero
    e->prt = (pm > 0.0001f) ? (pp / pm) : 0.0f;
    e->hist[0].prt = e->prt;
    e->hist[0].heater_sp = p->heater_sp;
    e->hist[0].piezo_v = p->piezo_v;

    // proteccion: si el heater se fue de rango, forzar la direccion opuesta
    // en el programa original esto tambien sacaba al sistema del lock
    if (p->heater_sp > p->heater_max) {
        e->modo = MODO_BACKWARD;
        p->lock_activo = false;
    }
    if (p->heater_sp < p->heater_min) {
        e->modo = MODO_FORWARD;
        p->lock_activo = false;
    }

    // calcular deltas segun el modo
    switch (e->modo) {
        case MODO_STOP:
            e->d_piezo = 0.0f;
            e->d_heater = 0.0f;
            break;

        case MODO_FORWARD:
            // avanzar: heater sube, piezo baja (si estuviera activo)
            e->d_piezo = -p->piezo_paso;
            e->d_heater = p->heater_paso_grueso;
            break;

        case MODO_BACKWARD:
            // retroceder: heater baja, piezo sube
            e->d_piezo = p->piezo_paso;
            e->d_heater = -p->heater_paso_grueso;
            break;

        case MODO_LOCK:
            // enganche automatico: mantener el ratio cerca de la referencia
            if (!p->lock_activo) {
                // al entrar en lock, la referencia es el ratio de hace 2 ciclos
                // esto viene del programa original (usa prtb2)
                p->prt_ref = e->hist[2].prt;
                p->lock_activo = true;
            }

            e->d_piezo = 0.0f;

            if (e->prt > p->prt_ref * p->coe) {
                // ratio alto, la longitud de onda se fue -> bajar heater
                e->d_heater = -p->heater_paso_fino;
            } else if (e->prt < p->prt_ref / p->coe) {
                // ratio bajo -> subir heater
                e->d_heater = p->heater_paso_fino;
            } else {
                // dentro de la banda muerta, no tocar
                e->d_heater = 0.0f;
            }
            break;

        case MODO_END:
            e->d_piezo = 0.0f;
            e->d_heater = 0.0f;
            break;
    }

    // aplicar los deltas al setpoint
    p->piezo_v += e->d_piezo;
    p->heater_sp += e->d_heater;
}

bool control_activo(ctrl_estado_t *e) {
    return e->modo != MODO_END;
}
