// seeder_comm.c : implementacion de la comunicacion con el seeder continuum
// usa uart1 a 57600 baud, 8n1, sin control de flujo
// los comandos son tipo scpi y terminan en \r\n

#include "seeder_comm.h"
#include "config.h"
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// timeout para esperar respuesta del seeder
#define SEEDER_TIMEOUT_MS 500

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

void seeder_enviar(const char *cmd) {
    echo_tx(cmd);
    uart_puts(SEEDER_UART, cmd);
}

int seeder_consultar(const char *cmd, char *resp, int max_len) {
    seeder_enviar(cmd);
    int n = leer_linea(resp, max_len);

    // el seeder a veces manda una respuesta corta o basura primero
    // si leimos muy poco, intentar una vez mas (igual que el original)
    if (n < 5) {
        n = leer_linea(resp, max_len);
    }
    limpiar_rx();

    if (n > 0) {
        printf("[rs232] << %s\n", resp);
    } else {
        printf("[rs232] << (sin respuesta)\n");
    }
    return n;
}

bool seeder_habilitar_protegido(void) {
    char resp[32];

    // mandar la contraseña para desbloquear comandos protegidos
    seeder_enviar(":syst:pass:cen \"NP\"\r\n");
    sleep_ms(100);

    // verificar que se habilito
    int n = seeder_consultar(":syst:pass:cen:stat?\r\n", resp, sizeof(resp));

    // el seeder responde "1" si quedo habilitado
    return (n > 0 && resp[0] == '1');
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
