// config.h : definiciones de pines y parametros por defecto
// aca se centraliza toda la configuracion del hardware

#ifndef CONFIG_H
#define CONFIG_H

// --- pines ---

// trigger del generador de ondas (entrada digital)
#define PIN_TRIGGER 2

// uart hacia el seeder continuum
#define PIN_SEEDER_TX 4
#define PIN_SEEDER_RX 5

// entradas analogicas para las señales del detector
#define PIN_ADC_PP 26   // gpio26 = adc0, señal p+
#define PIN_ADC_PM 27   // gpio27 = adc1, señal p-
#define ADC_CANAL_PP 0
#define ADC_CANAL_PM 1

// --- comunicacion con el seeder ---
#define SEEDER_UART uart1
#define SEEDER_BAUD 57600

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
#define HEATER_MIN 61.3f
#define HEATER_MAX 63.3f

// coeficiente de banda muerta para el enganche automatico
#define LOCK_COE 1.2f

#endif
