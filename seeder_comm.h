// seeder_comm.h : comunicacion serie con el seeder continuum
// protocolo basado en comandos de texto tipo scpi terminados en \r\n

#ifndef SEEDER_COMM_H
#define SEEDER_COMM_H

#include <stdbool.h>

// inicializa el uart y lo deja listo para hablar con el seeder
void seeder_init(void);

// CENable: manda el password "NP" para DESBLOQUEAR los comandos
// protegidos (tcon:spo, pdr:volt, *SAV, etc). el seeder arranca siempre
// bloqueado, tanto al encender como despues de un *RST.
// devuelve true si el seeder confirmo la habilitacion
bool seeder_habilitar_protegido(void);

// CDISable: vuelve a BLOQUEAR los comandos protegidos.
// operacion inversa de la anterior
void seeder_bloquear_protegido(void);

// estado crudo de :syst:pass:cen:stat?, sin interpretar.
// devuelve -1 si el seeder no contesto o contesto algo no numerico
int seeder_leer_estado_lock(void);

// fija el voltaje del piezo
void seeder_set_piezo(float voltaje);

// fija el setpoint de temperatura del heater
void seeder_set_heater(float temperatura);

// lee el voltaje actual del piezo que reporta el seeder
float seeder_leer_piezo(void);

// lee la temperatura actual del heater que reporta el seeder
float seeder_leer_heater(void);

// lee el SETPOINT de temperatura que tiene cargado el seeder.
// ojo que no es lo mismo que seeder_leer_heater(): esa devuelve la
// temperatura medida (tmon2), que tarda en seguir al setpoint por
// inercia termica. este valor cambia apenas el seeder acepta la
// escritura, asi que es el que sirve para verificar que entro
float seeder_leer_heater_sp(void);

// bit 0 de :syst:smod:flag? -> segundo modo detectado
// (FLM-005 tabla 5.8, todos los bits son activo alto)
#define SMOD_FLAG_SEGUNDO_MODO 0x01

// lee los flags de second mode del seeder (ver FLM-005 sec. 5.1.13).
// el seeder ya compara el nivel contra su umbral configurado, asi que
// esto viene resuelto: bit 0 en 1 = esta saliendo de modo unico.
// devuelve -1 si el seeder no contesto o contesto algo no numerico
int seeder_leer_smod_flags(void);

// valor devuelto por seeder_leer_error() cuando el seeder no contesta.
// no puede chocar con un codigo real: van de -32768 a +32767
#define SEEDER_ERR_SIN_RESPUESTA (-99999)

// lee y consume el error mas viejo de la cola del seeder (:syst:err?).
// la cola es fifo y guarda hasta 10 mensajes; codigo 0 = "No error",
// o sea que ya no queda nada por leer. ver FLM-005 sec. 5.1.3 y tabla 5.23.
// codigos utiles: -203 command protected, -102 syntax error,
// -121 invalid character in number, -222 data out of range
int seeder_leer_error(char *desc, int max_desc);

// envia un comando arbitrario al seeder (ya con \r\n incluido)
void seeder_enviar(const char *cmd);

// envia un query y espera la respuesta
// devuelve la cantidad de caracteres leidos en resp
int seeder_consultar(const char *cmd, char *resp, int max_len);

#endif
