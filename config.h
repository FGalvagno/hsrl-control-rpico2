// config.h : definiciones de pines y parametros por defecto
// aca se centraliza toda la configuracion del hardware

#ifndef CONFIG_H
#define CONFIG_H

// --- pines ---

// trigger del generador de ondas (entrada digital)
#define PIN_TRIGGER 13

// uart hacia el seeder continuum
#define PIN_SEEDER_TX 0
#define PIN_SEEDER_RX 1

// entradas analogicas para las señales del detector
#define PIN_ADC_PP 26   // gpio26 = adc0, señal p+
#define PIN_ADC_PM 27   // gpio27 = adc1, señal p-
#define ADC_CANAL_PP 0
#define ADC_CANAL_PM 1

// --- comunicacion con el seeder ---
#define SEEDER_UART uart0
#define SEEDER_BAUD 57600

// desbloqueo de comandos protegidos al arranque.
// se reintenta porque el seeder puede estar todavia inicializandose
// cuando el pico ya arranco, y ahi el primer intento se pierde
#define UNLOCK_MAX_INTENTOS 5
#define UNLOCK_REINTENTO_MS 2000

// medio periodo del parpadeo del led mientras se reintenta el desbloqueo.
// es la unica señal de vida en esa etapa: la consola usb todavia no esta
// conectada, asi que los printf de esos intentos no se ven
#define UNLOCK_PARPADEO_MS 200

// --- parametros de control por defecto ---

// voltaje piezo (queda fijo, no se controla en esta version)
#define DEFAULT_PIEZO_V 2.0f

// temperatura inicial del heater
#define DEFAULT_HEATER_SP 62.14f

// pasos de temperatura para el heater
#define HEATER_PASO_GRUESO 0.069923f   // para scanning rapido (modos f/b)
#define HEATER_PASO_MEDIO  0.01775f    // intermedio
#define HEATER_PASO_FINO   0.0044375f  // para el modo lock (g)

// limites de seguridad del heater
#define HEATER_MIN 62.5f
#define HEATER_MAX 63.1f

// coeficiente de banda muerta para el enganche automatico
#define LOCK_COE 1.2f

// --- promediado de mediciones ---

// cantidad de picos que se toman de p+ y de p- antes de mandar
// la temperatura nueva al seeder. con n=1 el comportamiento es el
// de siempre (una medicion por ciclo). se puede cambiar en caliente
// con el comando A del puerto serie.
#define DEFAULT_N_PROM 1

// tope para no colgar el ciclo esperando demasiados triggers
#define N_PROM_MAX 100

// --- espera de estabilizacion ---

// pausa antes de digitalizar, para que el heater alcance el setpoint
// que se le mando al final del ciclo anterior. se cambia en caliente
// con el comando E (en segundos). el original usaba 15 s fijos
#define DEFAULT_ESPERA_MS 10000

// tope para no dejar el ciclo detenido demasiado tiempo (10 min)
#define ESPERA_MAX_MS 600000

// --- sintonizacion automatica (modo a) ---

// barridos de ida y vuelta que hace el modo auto antes de decidir.
// mas barridos promedian mejor el ruido en la deteccion de los minimos,
// pero cada tramo cuesta ~29 puntos por la espera de E. se cambia con S
#define DEFAULT_N_BARRIDOS 1
#define N_BARRIDOS_MAX 20

// tope de puntos por tramo, por si alguien deja un paso muy chico
// con el comando R y el barrido se vuelve eterno
#define AUTO_PASOS_MAX 2000

// el salto al extremo inferior (y el final al punto de operacion) son
// excursiones mucho mas grandes que un paso: se espera este multiplo
#define AUTO_ESPERA_LARGA_X 3

#endif
