// seeder_comm.c : implementacion de la comunicacion con el seeder continuum
// usa uart1 a 57600 baud, 8n1, sin control de flujo
// los comandos son tipo scpi y terminan en \r\n

#include "seeder_comm.h"
#include "config.h"
#include "pico/stdlib.h"
#include "pico/mutex.h"
#include "hardware/uart.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// timeout para esperar respuesta del seeder
#define SEEDER_TIMEOUT_MS 500

// el uart lo usan los dos cores: core0 desde el loop de medicion y
// core1 desde el comando '!'. sin este lock una consulta puede quedar
// leyendo la respuesta del otro core y devolver basura.
// es un mutex y no una critical_section porque una transaccion puede
// tardar cientos de ms y no queremos las irq apagadas todo ese tiempo.
static mutex_t g_uart_mtx;

// lee una linea de respuesta del seeder, corta por \n o por timeout
static int leer_linea(char *buf, int max_len) {
    int i = 0;
    absolute_time_t limite = make_timeout_time_ms(SEEDER_TIMEOUT_MS);

    while (i < max_len - 1) {
        // si se paso el tiempo, salir con lo que haya
        if (absolute_time_diff_us(get_absolute_time(), limite) < 0) {
            break;
        }
        if (uart_is_readable(SEEDER_UART)) {
            char c = uart_getc(SEEDER_UART);
            if (c == '\n' || c == '\r') {
                if (i > 0) break;   // fin de linea
                continue;           // ignorar \r\n sueltos al principio
            }
            buf[i++] = c;
        }
    }
    buf[i] = '\0';
    return i;
}

// descarta todo lo que haya en el buffer de recepcion
static void limpiar_rx(void) {
    while (uart_is_readable_within_us(SEEDER_UART, 1000)) {
        uart_getc(SEEDER_UART);
    }
}

void seeder_init(void) {
    mutex_init(&g_uart_mtx);

    uart_init(SEEDER_UART, SEEDER_BAUD);
    gpio_set_function(PIN_SEEDER_TX, UART_FUNCSEL_NUM(SEEDER_UART, PIN_SEEDER_TX));
    gpio_set_function(PIN_SEEDER_RX, UART_FUNCSEL_NUM(SEEDER_UART, PIN_SEEDER_RX));
    
    // 8 bits de datos, 1 bit de stop, sin paridad
    uart_set_format(SEEDER_UART, 8, 1, UART_PARITY_NONE);
    uart_set_hw_flow(SEEDER_UART, false, false);
}

// imprime el comando por usb sin el \r\n final
static void echo_tx(const char *cmd) {
    // copiar hasta el primer \r o \n para no ensuciar la consola
    char limpio[64];
    int i = 0;
    while (cmd[i] && cmd[i] != '\r' && cmd[i] != '\n' && i < (int)(sizeof(limpio) - 1)) {
        limpio[i] = cmd[i];
        i++;
    }
    limpio[i] = '\0';
    printf("[rs232] >> %s\n", limpio);
}

// escribe al uart sin tomar el lock: solo para uso interno,
// el que llama tiene que tenerlo tomado
static void enviar_raw(const char *cmd) {
    echo_tx(cmd);
    uart_puts(SEEDER_UART, cmd);
}

void seeder_enviar(const char *cmd) {
    mutex_enter_blocking(&g_uart_mtx);
    enviar_raw(cmd);
    mutex_exit(&g_uart_mtx);
}

int seeder_consultar(const char *cmd, char *resp, int max_len) {
    // el envio y la lectura de la respuesta van juntos bajo el mismo
    // lock: si se separan, el otro core puede meter su comando en el
    // medio y cada uno se lleva la respuesta del otro
    mutex_enter_blocking(&g_uart_mtx);

    enviar_raw(cmd);
    int n = leer_linea(resp, max_len);

    // el seeder a veces manda una respuesta corta o basura primero.
    // si leimos poco reintentamos, igual que el original, PERO sobre un
    // buffer aparte: si el reintento vence por timeout no queremos pisar
    // con vacio la respuesta buena que ya teniamos (era el caso de
    // ":syst:pass:cen:stat?", que contesta un solo caracter)
    if (n < 5) {
        char alt[64];
        int n2 = leer_linea(alt, sizeof(alt));
        if (n2 > 0) {
            int lim = (n2 < max_len - 1) ? n2 : max_len - 1;
            memcpy(resp, alt, lim);
            resp[lim] = '\0';
            n = lim;
        }
    }
    limpiar_rx();

    mutex_exit(&g_uart_mtx);

    if (n > 0) {
        printf("[rs232] << %s\n", resp);
    } else {
        printf("[rs232] << (sin respuesta)\n");
    }
    return n;
}

int seeder_leer_estado_lock(void) {
    char resp[32];

    int n = seeder_consultar(":syst:pass:cen:stat?\r\n", resp, sizeof(resp));
    if (n <= 0) return -1;
    if (resp[0] < '0' || resp[0] > '9') return -1;

    return atoi(resp);
}

void seeder_bloquear_protegido(void) {
    // CDISable: vuelve a proteger los comandos de configuracion.
    // es la operacion inversa de seeder_habilitar_protegido()
    seeder_enviar(":syst:pass:cdis \"NP\"\r\n");
    sleep_ms(100);
}

bool seeder_habilitar_protegido(void) {
    // *CLS antes que nada: limpia la cola de errores y, sobre todo, sirve
    // de comando "sacrificable". al hacer gpio_set_function() sobre el pin
    // de tx puede salir un flanco espurio que el seeder lee como un byte
    // basura, y ese byte corrompe la primera linea que mandemos. si esa
    // linea es el password, el unlock no entra y despues las queries
    // andan igual, con lo cual el sintoma es dificil de ver
    seeder_enviar("*CLS\r\n");
    sleep_ms(100);

    // mandar la contraseña para desbloquear comandos protegidos
    seeder_enviar(":syst:pass:cen \"NP\"\r\n");
    sleep_ms(100);

    // verificar que se habilito. ojo con la polaridad: el PDF se
    // contradice entre "state of the protected commands" y "protection
    // state", asi que este 1 hay que confirmarlo contra el hardware
    return (seeder_leer_estado_lock() == 1);
}

void seeder_set_piezo(float voltaje) {
    char cmd[40];
    snprintf(cmd, sizeof(cmd), ":syst:pdr:volt %f\r\n", voltaje);
    seeder_enviar(cmd);
}

void seeder_set_heater(float temperatura) {
    char cmd[40];
    snprintf(cmd, sizeof(cmd), ":syst:tcon2:spo %f\r\n", temperatura);
    seeder_enviar(cmd);
}

float seeder_leer_piezo(void) {
    char resp[32];
    seeder_consultar(":syst:pdr:volt?\r\n", resp, sizeof(resp));
    return strtof(resp, NULL);
}

float seeder_leer_heater(void) {
    char resp[32];
    seeder_consultar(":syst:tmon2:temp?\r\n", resp, sizeof(resp));
    return strtof(resp, NULL);
}

float seeder_leer_heater_sp(void) {
    char resp[32];
    seeder_consultar(":syst:tcon2:spo?\r\n", resp, sizeof(resp));
    return strtof(resp, NULL);
}

// igual que seeder_consultar pero con una sola lectura, sin el reintento.
// wvcnt_lec.cpp tampoco reintenta para los queries de smod, y por buen
// motivo: la respuesta es de 1 o 2 digitos, asi que el "if (n < 5)" de
// seeder_consultar se dispararia siempre y la segunda lectura vaciaria
// el buffer al vencer el timeout, borrando el valor bueno
static int consultar_sin_reintento(const char *cmd, char *resp, int max_len) {
    mutex_enter_blocking(&g_uart_mtx);

    enviar_raw(cmd);
    int n = leer_linea(resp, max_len);
    limpiar_rx();

    mutex_exit(&g_uart_mtx);

    if (n > 0) {
        printf("[rs232] << %s\n", resp);
    } else {
        printf("[rs232] << (sin respuesta)\n");
    }
    return n;
}

int seeder_leer_smod_flags(void) {
    char resp[16];

    // el original espera 100 ms antes de mandar el query de smod.
    // va fuera del lock para no retener el uart al pedo
    sleep_ms(100);

    int n = consultar_sin_reintento(":syst:smod:flag?\r\n", resp, sizeof(resp));
    if (n <= 0) return -1;

    // atoi devuelve 0 tanto para "0" como para basura, asi que
    // chequeamos que arranque con algo numerico para poder distinguir
    char c = resp[0];
    if (!((c >= '0' && c <= '9') || c == '+' || c == '-')) return -1;

    return atoi(resp);
}

int seeder_leer_error(char *desc, int max_desc) {
    char resp[64];

    if (desc && max_desc > 0) desc[0] = '\0';

    // este query no esta protegido, siempre deberia contestar
    int n = seeder_consultar(":syst:err?\r\n", resp, sizeof(resp));
    if (n <= 0) return SEEDER_ERR_SIN_RESPUESTA;

    // formato de respuesta: <codigo>,"<descripcion>"
    int codigo = atoi(resp);

    const char *coma = strchr(resp, ',');
    if (coma && desc && max_desc > 0) {
        const char *p = coma + 1;
        if (*p == '"') p++;            // saltar la comilla de apertura

        int i = 0;
        while (*p && *p != '"' && i < max_desc - 1) {
            desc[i++] = *p++;
        }
        desc[i] = '\0';
    }

    return codigo;
}
