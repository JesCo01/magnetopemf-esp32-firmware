#ifndef THERAPY_PRESETS_H
#define THERAPY_PRESETS_H

#include "Config.h"
#include "MagnetoTypes.h"

// Configuraciones de ensayo; no representan prescripciones ni campos medidos.
inline const TherapyPreset PRESETS[PRESET_COUNT] = {
    {0,"PWM manual",50,10,WaveShape::PWM_PURE,5,0,"ENSAYO","Configuracion de banco"},
    {1,"PWM 75 Hz",75,10,WaveShape::PWM_PURE,5,0,"ENSAYO","Configuracion de banco"},
    {2,"Rafaga 15 Hz",15,7.5,WaveShape::BURST,5,0,"ENSAYO","Ventana 5 ms, 20 micropulsos"},
    {3,"PWM 50 Hz",50,40,WaveShape::PWM_PURE,5,0,"ENSAYO","Configuracion de banco"},
    {4,"PWM 15 Hz",15,10,WaveShape::PWM_PURE,5,0,"ENSAYO","Configuracion de banco"},
    {5,"Rafaga fija",15,7.5,WaveShape::BURST,5,0,"ENSAYO","Ventana 5 ms, 20 micropulsos"}
};

/**
 * @brief Calcula el duty cycle raw de 16 bits a partir de un porcentaje.
 * @param pct Porcentaje (0.0 - 100.0)
 * @return Valor 0-65535
 */
inline uint16_t pctToDuty16(float pct) {
    if (pct < 0.0f)   pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    return (uint16_t)(pct / 100.0f * (float)PWM_MAX_DUTY);
}

/**
 * @brief Convierte duty16 a porcentaje decimal.
 */
inline float duty16ToPct(uint16_t duty) {
    return (float)duty / (float)PWM_MAX_DUTY * 100.0f;
}

#endif // THERAPY_PRESETS_H
