#ifndef MAGNETO_TYPES_H
#define MAGNETO_TYPES_H

#include <Arduino.h>
#include "Config.h"

// ESTADOS DEL SISTEMA (FSM)
enum class SystemState {
    IDLE,
    RUNNING,
    PAUSED,
    LOW_BATTERY,
    ERROR
};

// FORMA DE ONDA
// Eficacia clínica por dB/dt: Triangular > Trapezoidal > Cuadrada > Senoidal
// Fuente: Principio de Faraday, Faldini 2010, Orthofix EBI specs
enum class WaveShape : uint8_t {
    SQUARE      = 0,  // Cuadrada: simple, rápida de implementar con LEDC
    TRIANGULAR  = 1,  // Cuasi-triangular: protocolo IGEA/Faldini ← RECOMENDADA
    TRAPEZOIDAL = 2,  // Trapezoidal: protocolo Orthofix/EBI
    BURST       = 3,  // Ráfaga de micro-pulsos (225µs) dentro de ventana (5.46ms)
    PWM_PURE    = 4   // PWM puro por LEDC en GPIO04: frecuencia + duty directos
};

// PARÁMETROS DE UN CANAL
// Extensible: añadir campos sin romper la interfaz binaria.
struct MagnetoChannelParams {
    uint32_t  frequency;   // Hz — portadora principal (1-100 Hz ELF)
    uint16_t  duty;        // Duty cycle MAXIMO raw (0-65535). Techo de la envolvente.
                           //   IGEA:    duty ≈  6390  (9.75%  de 65535)
                           //   Orthofix: duty ≈  5367  (8.19%  de 65535)
    uint16_t  dutyMin;     // Duty cycle MINIMO raw. Piso de la envolvente (ej. 5%).
    WaveShape waveform;    // Forma de onda (ver enum WaveShape)
    int16_t   phase;       // Desfase en grados (0-360). Útil para cuadratura entre bobinas.
    bool      enabled;     // Habilitar/deshabilitar canal sin detener el tratamiento
};

// Copia completa para validar una orden antes de modificar canales o salidas.
struct ControlConfiguration {
    MagnetoChannelParams channels[PWM_CH_COUNT];
    uint32_t burstFrequency[PWM_CH_COUNT];
    uint8_t presetId;
};

// ESTADO DEL TRATAMIENTO ACTIVO
struct TreatmentStatus {
    bool          isActive;
    unsigned long startTime;       // millis() del ultimo arranque/reanudacion
    unsigned long totalDurationMs; // Duración total programada
    unsigned long elapsedBeforePauseMs;
    unsigned long remainingMs;
    int           remainingMinutes;
};

// PROTOCOLO / PRESET CLÍNICO
// Basado en evidencia publicada. Ver Config.h para IDs y fuentes.
struct TherapyPreset {
    uint8_t   id;
    const char* name;
    uint32_t  frequency;         // Hz
    float     dutyPct;           // % micro duty cycle (8-10% clínico)
    WaveShape waveform;
    int       sessionMinutes;    // Duración de sesión clínica sugerida
    float     fieldTargetMT;     // mT objetivo (referencial, requiere calibración)
    const char* evidence;        // Nivel de evidencia
    const char* source;          // Referencia bibliográfica
};

// TABLA DE PRESETS — Definida en TherapyPresets.h
// Acceso: PRESETS[PRESET_IGEA].frequency
extern const TherapyPreset PRESETS[];

#endif // MAGNETO_TYPES_H
