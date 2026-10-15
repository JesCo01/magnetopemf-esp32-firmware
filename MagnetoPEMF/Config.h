/** Configuracion del prototipo de banco: PWM 10-100 Hz, 10-50 %, sesiones 1-10 min. */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <driver/ledc.h>
#include <driver/rmt_tx.h>
#include <driver/gptimer.h>

// BLE
#define DEVICE_NAME          "MAGNETO_TESIS"
#define SERVICE_UUID         "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID  "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// PWM HARDWARE — MH-ET LIVE ESP32 MiniKit (ESP32-WROOM-32)
// Configuración de 1 canal PWM → DRV8871 IN1
// IN2 del DRV8871 debe conectarse a GND (operación unidireccional)
// GPIO22: salida segura, no es strapping pin, compatible con GPIO Matrix.
#define PWM_CH_COUNT      1
#define PWM_RESOLUTION    LEDC_TIMER_16_BIT   // 0-65535
#define PWM_SPEED_MODE    LEDC_LOW_SPEED_MODE
#define PWM_MAX_DUTY      65535U

// RMT para modo BURST (micropulsos exactos)
#define RMT_RESOLUTION_HZ 1000000U   // 1 MHz = 1 µs por tick
#define BURST_MAX_SYMBOLS  48        // Máximo símbolos RMT por ráfaga

struct PwmPinConfig {
    gpio_num_t pin;
    ledc_channel_t channel;
    ledc_timer_t timer;
};

// Único canal: GPIO 4 → MOSFET/Driver → Bobina
// Este mismo GPIO se comparte entre LEDC (square/triangular) y RMT (burst)
const PwmPinConfig PWM_PINS[PWM_CH_COUNT] = {
    {GPIO_NUM_4, LEDC_CHANNEL_0, LEDC_TIMER_0}
};

// --- Parámetros de Terapia y Seguridad ---
#define FREQ_MIN_HZ        10
#define FREQ_MAX_HZ        100
#define DUTY_MIN_PCT       10.0f
#define DUTY_MAX_PCT       90.0f    // Limite por software para pruebas

// --- SPWM (Modulación de Ancho de Pulso para ondas Triangular/Trapezoidal) ---
// La portadora LEDC corre a alta frecuencia; un timer la modula para
// trazar la forma de onda deseada dentro de cada ciclo terapéutico.
// NOTA: A 16 bits el ESP32 (80MHz APB) solo soporta ~1220 Hz max.
//       A 12 bits soporta hasta ~19.5 kHz. Usamos 10 kHz por margen.
#define SPWM_CARRIER_HZ    10000U                // 10 kHz portadora (inaudible)
#define SPWM_RESOLUTION    LEDC_TIMER_12_BIT     // 12 bits = 0-4095
#define SPWM_MAX_DUTY      4095U                 // Máximo duty a 12 bits

// --- Pines Analógicos (DAC) Removidos ---
// Todo el tráfico se enruta ahora digitalmente al GPIO 4.

// Banco 4S: limites de software y conversion nominal 68k/10k.
// BAT_CAL_GAIN/OFFSET permiten incorporar una calibracion externa medida.
#define BAT_MIN_VOLT         12.0f
#define BAT_MAX_VOLT         16.8f
#define BAT_DIVIDER_RATIO     7.8f
#define BAT_CAL_GAIN          1.0f
#define BAT_CAL_OFFSET_V      0.0f
#define BAT_SEND_INTERVAL_MS 5000UL
#define BAT_LOW_THRESHOLD    15   // % para alerta
#define BAT_CRITICAL_TH      5    // % para apagado de emergencia
#define BAT_NOISE_FLOOR_V    0.35f // Voltaje por debajo del cual se considera pin flotante (sin batería)

// PARÁMETROS CLÍNICOS PEMF — Ventanas terapéuticas validadas
// Fuente: meta-análisis, Faldini 2010, Akdag, Hannemann, OMS

// --- Frecuencias terapéuticas (ELF: < 100 Hz) ---
// Solo se permiten estos tres valores discretos en los presets clínicos.
// Un rango continuo no tiene sustento clínico en la literatura.
// FREQ_MIN_HZ y FREQ_MAX_HZ definidos arriba en la sección de seguridad.
#define FREQ_IGEA_HZ      75U      // Protocolo IGEA / Faldini — nivel evidencia: ALTO
#define FREQ_ORTHOFIX_HZ  15U      // Protocolo Orthofix / EBI — nivel evidencia: ALTO
#define FREQ_ANTI_INF_HZ  50U      // Antiinflamatorio / crónico — nivel evidencia: MEDIO

// --- Micro duty cycle (dB/dt alto → onda cuasi-triangular/trapezoidal) ---
// Derivados matemáticamente de la literatura:
//   IGEA 75 Hz: pulse 1.3 ms / period 13.33 ms = 9.75%
//   Orthofix 15 Hz: burst 5.46 ms / period 66.66 ms = 8.19%
// Formula: duty_raw = pct * PWM_MAX_DUTY / 100
#define DUTY_IGEA_PCT     9.75f    // ≈ 6390 en escala 16 bits
#define DUTY_ORTHOFIX_PCT 8.19f    // ≈ 5367 en escala 16 bits
#define DUTY_BURST_5MS_PCT 7.50f   // 5.00 ms / 66.67 ms = 7.5%
#define DUTY_DEFAULT_PCT  10.0f    // Punto de partida seguro para usuario
// DUTY_MIN_PCT y DUTY_MAX_PCT definidos arriba en la sección de seguridad.

// --- Intensidad de campo (mT) — Solo referencial; requiere magnetómetro para calibración ---
// El firmware NO controla directamente mT, sino el duty que lo produce.
// Estos valores se usan para documentación y logging.
#define FIELD_TARGET_MIN_MT  1.0f  // mT mínimo terapéutico (evidencia: ALTO)
#define FIELD_TARGET_MAX_MT  3.0f  // mT máximo recomendado (evidencia: ALTO)
#define FIELD_SAFETY_MAX_MT  50.0f // mT límite OMS a 50-60 Hz → PROHIBIDO superarlo
#define FIELD_IGEA_MT        2.0f  // mT protocolo IGEA
#define FIELD_ORTHOFIX_MT    1.5f  // mT protocolo Orthofix/EBI

// --- Tiempos de sesión ---
#define SESSION_MIN_MIN      1     // 1 min mínimo (sin restricciones para pruebas)
#define SESSION_DEFAULT_MIN  5    // Duracion inicial de ensayo
#define SESSION_MAX_MIN      10   // Maximo para ensayo de banco
#define SESSION_SAFETY_MAX_MIN 10 // Limite temporal del prototipo

// MODO RÁFAGA (BURST MODE) — Protocolo Orthofix/EBI
// Orthofix: 15 Hz portadora | ráfaga 5.46 ms | micro-pulsos 225 µs dentro de la ráfaga
// Implementado como envolvente a 15 Hz con micropulsos PWM internos
#define BURST_PULSE_WIDTH_US  225   // µs — duración del micro-pulso (Orthofix)
#define BURST_GAP_US          25    // µs — pausa entre micro-pulsos dentro de la ráfaga
#define BURST_WINDOW_MS       5     // ms — ventana de ráfaga (≈5.46 ms Orthofix)
#define BURST_MIN_WINDOW_US   5000  // µs — ventana mínima útil para pruebas 15 Hz
#define BURST_ENABLED         true  // BURST aproximado habilitado en firmware

// PRESETS CLÍNICOS (ID numérico para protocolo rápido vía BLE)
// Uso desde app: {"cmd":"preset","id":1}
#define PRESET_CUSTOM       0  // Sin preset. Parámetros libres del usuario.
#define PRESET_IGEA         1  // IGEA/Faldini: 75 Hz, 9.75% duty, 480 min, triangular
#define PRESET_ORTHOFIX     2  // Orthofix/EBI: 15 Hz, 8.19% duty, 480 min, burst
#define PRESET_ANTI_INFLAM  3  // Antiinflamatorio: 50 Hz, 10% duty, 30 min, trapezoidal
#define PRESET_PRECLINICO   4  // Parate in vitro: 15 Hz, 2mT equiv., 10 min (investigación)
#define PRESET_BURST_QUICK  5  // Burst fijo 15 Hz | 5 ms | 5 min
#define PRESET_COUNT        6

// PROTOCOLO JSON (ArduinoJson)
#define JSON_BUFFER_SIZE    512

// Comandos
#define CMD_START   "start"
#define CMD_PAUSE   "pause"
#define CMD_RESUME  "resume"
#define CMD_STOP    "stop"
#define CMD_SET     "set"
#define CMD_STATUS  "status"
#define CMD_PRESET  "preset"   // {"cmd":"preset","id":1}

// FORMAS DE ONDA (WAVE_SHAPE)
// Ranking de eficacia clínica (dB/dt mayor = mayor campo inducido):
//   Triangular > Trapezoidal > Cuadrada > Senoidal
// Fuente: Principio de Faraday aplicado a PEMF (nivel evidencia: ALTO)
#define WAVE_SQUARE      0   // Cuadrada (simple, rápida de implementar)
#define WAVE_TRIANGULAR  1   // Triangular/cuasi-triangular (IGEA, Faldini) ← PREFERIDA
#define WAVE_TRAPEZOIDAL 2   // Trapezoidal (Orthofix) ← segunda opción
#define WAVE_BURST       3   // Ráfaga de micro-pulsos (Orthofix full mode)
#define WAVE_PWM_PURE    4   // PWM puro y duro: LEDC directo en GPIO04

// DEPURACIÓN
#define DEBUG_SERIAL_BAUD  115200

#endif // CONFIG_H
