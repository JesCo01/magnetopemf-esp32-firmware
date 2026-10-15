#include "BatteryMonitor.h"
#include <driver/adc.h>

BatteryMonitor::BatteryMonitor() : _voltage(0), _percentage(0), _rawAdc(0), _lastRead(0) {}

void BatteryMonitor::begin() {
    // Configurar ADC para la bateria en GPIO34 / ADC1_CHANNEL_6.
    analogReadResolution(12);
    analogSetPinAttenuation(34, ADC_11db);

    // Lectura inicial.
    update();
}

void BatteryMonitor::update() {
    unsigned long now = millis();
    if (now - _lastRead >= 2000 || _lastRead == 0) {
        _voltage = readRawVoltage();
        _percentage = calculatePercentage(_voltage);
        _lastRead = now;
    }
}

float BatteryMonitor::readRawVoltage() {
    // Promedio de diez conversiones calibradas del ADC, sin delay de milisegundos.
    long sum = 0;
    const int samples = 10;
    for (int i = 0; i < samples; i++) {
        sum += analogReadMilliVolts(34);
        delayMicroseconds(100);  // 10 × 100µs = 1ms total (vs 20ms antes)
    }

    _rawAdc = analogRead(34);

    // Tension del banco: lectura calibrada del pin por relacion nominal del divisor.
    return (float(sum) / samples / 1000.0f) * BAT_DIVIDER_RATIO * BAT_CAL_GAIN + BAT_CAL_OFFSET_V;
}

int BatteryMonitor::calculatePercentage(float voltage) {
    // Si el voltaje está por debajo del piso de ruido, no hay batería conectada.
    // El pin GPIO34 flotante genera ~0.2-0.3V de ruido electromagnético.
    if (voltage < BAT_NOISE_FLOOR_V) return 0;

    if (voltage < BAT_MIN_VOLT) return 0;
    if (voltage >= BAT_MAX_VOLT) return 100;

    // Indicador lineal por tension entre 12.0 y 16.8 V; no mide capacidad restante.
    float scaled = (voltage - BAT_MIN_VOLT) * 99.0f / (BAT_MAX_VOLT - BAT_MIN_VOLT);
    int pct = 1 + (int)(scaled + 0.5f);
    return constrain(pct, 1, 100);
}
