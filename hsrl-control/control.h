// control.h : logica de control de longitud de onda del seeder
// replica el comportamiento del programa original wvcnt_lec.cpp
// pero simplificado para usar solo p+ y p- medidos por adc

#ifndef CONTROL_H
#define CONTROL_H

#include <stdbool.h>

// modos de operacion del controlador
typedef enum {
    MODO_STOP     = 's',    // detenido, no mueve nada
    MODO_FORWARD  = 'f',    // sube temperatura heater (longitud de onda crece)
    MODO_BACKWARD = 'b',    // baja temperatura heater (longitud de onda decrece)
    MODO_LOCK     = 'g',    // enganche automatico con ratio p+/p-
    MODO_END      = 'e'     // terminar el programa
} modo_t;

// parametros ajustables del controlador
typedef struct {
    // piezo (queda fijo en esta version)
    float piezo_v;
    float piezo_paso;

    // heater
    float heater_sp;            // setpoint actual
    float heater_paso_grueso;   // paso para scanning (modos f y b)
    float heater_paso_medio;
    float heater_paso_fino;     // paso para lock (modo g)
    float heater_min;           // limite inferior de seguridad
    float heater_max;           // limite superior de seguridad

    // enganche automatico
    float coe;                  // coeficiente de banda muerta
    float prt_ref;              // ratio de referencia al entrar en lock
    bool lock_activo;           // indica si ya se capturo la referencia
} ctrl_params_t;

// registro de un ciclo de medicion (para el historial)
typedef struct {
    float pp;
    float pm;
    float prt;
    float heater_sp;
    float piezo_v;
} ctrl_historial_t;

// estado actual del controlador
typedef struct {
    modo_t modo;
    float d_piezo;              // delta aplicado al piezo en este ciclo
    float d_heater;             // delta aplicado al heater en este ciclo
    float prt;                  // ratio p+/p- actual
    ctrl_historial_t hist[3];   // [0]=actual, [1]=anterior, [2]=hace dos ciclos
} ctrl_estado_t;

// inicializa parametros y estado con valores por defecto
void control_init(ctrl_params_t *p, ctrl_estado_t *e);

// procesa un caracter de comando del usuario
void control_comando(char c, ctrl_params_t *p, ctrl_estado_t *e);

// toma las mediciones, calcula los deltas y actualiza los setpoints
void control_actualizar(float pp, float pm, ctrl_params_t *p, ctrl_estado_t *e);

// devuelve true mientras el programa deba seguir corriendo
bool control_activo(ctrl_estado_t *e);

#endif
