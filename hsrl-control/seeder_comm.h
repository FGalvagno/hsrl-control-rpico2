// seeder_comm.h : comunicacion serie con el seeder continuum
// protocolo basado en comandos de texto tipo scpi terminados en \r\n

#ifndef SEEDER_COMM_H
#define SEEDER_COMM_H

#include <stdbool.h>

// inicializa el uart y lo deja listo para hablar con el seeder
void seeder_init(void);

// habilita comandos protegidos en el seeder (password "NP")
// devuelve true si el seeder confirmo la habilitacion
bool seeder_habilitar_protegido(void);

// fija el voltaje del piezo
void seeder_set_piezo(float voltaje);

// fija el setpoint de temperatura del heater
void seeder_set_heater(float temperatura);

// lee el voltaje actual del piezo que reporta el seeder
float seeder_leer_piezo(void);

// lee la temperatura actual del heater que reporta el seeder
float seeder_leer_heater(void);

// envia un comando arbitrario al seeder (ya con \r\n incluido)
void seeder_enviar(const char *cmd);

// envia un query y espera la respuesta
// devuelve la cantidad de caracteres leidos en resp
int seeder_consultar(const char *cmd, char *resp, int max_len);

#endif
