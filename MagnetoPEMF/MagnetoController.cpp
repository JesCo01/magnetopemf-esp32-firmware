#include "MagnetoController.h"
#include <esp_timer.h>
#include <math.h>

// Constructor
MagnetoController::MagnetoController()
    : _state(SystemState::IDLE),
      _activePresetId(PRESET_CUSTOM),
      _rmtChannel(nullptr),
      _rmtEncoder(nullptr),
      _macroTimer(nullptr),
      _burstModeActive(false),
      _burstMacroHz(0),
      _spwmActive(false),
      _spwmPhaseUs(0),
      _spwmPeriodUs(0),
      _spwmHalfPeriodUs(0),
      _spwmQ1Us(0),
      _spwmQ3Us(0),
      _spwmDutyMin(0),
      _spwmDutyMax(0),
      _spwmShape(WaveShape::SQUARE),
      _spwmChannelIdx(0) {
    for (int i = 0; i < PWM_CH_COUNT; i++) {
        _channels[i] = {75, pctToDuty16(DUTY_DEFAULT_PCT), pctToDuty16(DUTY_MIN_PCT), WaveShape::SQUARE, 0, true};
        _burstFreq[i] = 0;
    }
    _treatment = {false, 0, 0, 0, 0, 0};
    _runStartUs = 0;
    _elapsedBeforePauseUs = 0;
}

// begin — Inicializa LEDC + RMT
void MagnetoController::begin() {
    // LEDC para modos SQUARE / PWM_PURE
    for (int i = 0; i < PWM_CH_COUNT; i++) {
        ledc_timer_config_t tc = {
            .speed_mode = PWM_SPEED_MODE,
            .duty_resolution = PWM_RESOLUTION,
            .timer_num = PWM_PINS[i].timer,
            .freq_hz = _channels[i].frequency,
            .clk_cfg = LEDC_AUTO_CLK,
        };
        ledc_timer_config(&tc);

        ledc_channel_config_t cc = {
            .gpio_num = PWM_PINS[i].pin,
            .speed_mode = PWM_SPEED_MODE,
            .channel = (ledc_channel_t)PWM_PINS[i].channel,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = PWM_PINS[i].timer,
            .duty = 0,
            .hpoint = 0,
        };
        ledc_channel_config(&cc);
    }

    // DAC removido (envolventes PWM en su lugar)

    // RMT se inicializa bajo demanda en startBurstMode()
    _rmtChannel = nullptr;
    _rmtEncoder = nullptr;

    Serial.println("[PWM] MagnetoController iniciado. Canales: " + String(PWM_CH_COUNT));
}

// initRmt — Configura canal RMT TX en GPIO 4, 1 MHz
void MagnetoController::initRmt() {
    rmt_tx_channel_config_t tx_cfg = {};
    tx_cfg.gpio_num = (gpio_num_t)PWM_PINS[0].pin;
    tx_cfg.clk_src = RMT_CLK_SRC_DEFAULT;
    tx_cfg.resolution_hz = RMT_RESOLUTION_HZ;
    tx_cfg.mem_block_symbols = 64;
    tx_cfg.trans_queue_depth = 4;

    esp_err_t err = rmt_new_tx_channel(&tx_cfg, &_rmtChannel);
    if (err != ESP_OK) {
        Serial.printf("[RMT] ERROR creando canal TX: %d\n", (int)err);
        _rmtChannel = nullptr;
        return;
    }

    rmt_copy_encoder_config_t enc_cfg = {};
    err = rmt_new_copy_encoder(&enc_cfg, &_rmtEncoder);
    if (err != ESP_OK) {
        Serial.printf("[RMT] ERROR creando encoder: %d\n", (int)err);
        _rmtEncoder = nullptr;
        return;
    }

    Serial.printf("[RMT] Canal inicializado: GPIO %d | %lu Hz\n",
                  PWM_PINS[0].pin, (unsigned long)RMT_RESOLUTION_HZ);
}

// buildBurstPattern — Construye la plantilla de micropulsos RMT
void MagnetoController::buildBurstPattern(int idx) {
    uint32_t pulseHighUs = BURST_PULSE_WIDTH_US;  // 225 µs
    uint32_t pulseLowUs  = BURST_GAP_US;          // 25 µs
    uint32_t windowUs    = (uint32_t)BURST_WINDOW_MS * 1000UL; // 5000 µs

    // Si hay burst_freq personalizada, recalcular tiempos
    if (_burstFreq[idx] > 0) {
        uint32_t totalPulseUs = 1000000UL / _burstFreq[idx];
        pulseHighUs = totalPulseUs * 9 / 10; // 90% HIGH
        pulseLowUs  = totalPulseUs - pulseHighUs;
    }

    uint32_t pulseTotal = pulseHighUs + pulseLowUs;
    if (pulseTotal == 0) pulseTotal = 250;

    size_t numPulses = windowUs / pulseTotal;
    if (numPulses > BURST_MAX_SYMBOLS) numPulses = BURST_MAX_SYMBOLS;
    if (numPulses == 0) numPulses = 1;

    for (size_t i = 0; i < numPulses; i++) {
        _burstSymbols[i].duration0 = (uint16_t)pulseHighUs;
        _burstSymbols[i].level0    = 1;
        _burstSymbols[i].duration1 = (uint16_t)pulseLowUs;
        _burstSymbols[i].level1    = 0;
    }
    _burstSymbolCount = numPulses;

    Serial.printf("[BURST] Plantilla: %u micropulsos | %luus HIGH + %luus LOW | ventana %luus\n",
                  (unsigned)numPulses, (unsigned long)pulseHighUs,
                  (unsigned long)pulseLowUs, (unsigned long)windowUs);
}

// onMacroTimerAlarm — ISR del timer hardware (contexto ISR)
bool IRAM_ATTR MagnetoController::onMacroTimerAlarm(
    gptimer_handle_t timer,
    const gptimer_alarm_event_data_t *edata,
    void *user_data)
{
    MagnetoController *self = static_cast<MagnetoController*>(user_data);
    self->_burstTrigger = true;
    return false; // no need to yield
}

// startBurstMode — Crea RMT bajo demanda, desactiva LEDC, arma Timer + RMT
void MagnetoController::startBurstMode(int idx) {
    // 1. Detener SPWM, LEDC y liberar los pines
    stopSpwm();
    stopLedc(idx);

    // 2. Si RMT no existe, crearlo ahora (lazy init)
    if (_rmtChannel == nullptr) {
        initRmt();
        if (_rmtChannel == nullptr) {
            Serial.println("[BURST] ERROR: No se pudo crear RMT");
            return;
        }
    }

    // 3. Construir plantilla de micropulsos
    buildBurstPattern(idx);

    // 4. Habilitar canal RMT
    esp_err_t err = rmt_enable(_rmtChannel);
    if (err != ESP_OK) {
        Serial.printf("[BURST] ERROR habilitando RMT: %d\n", (int)err);
        return;
    }

    // 5. Crear o reconfigurar el GPTimer macro
    _burstMacroHz = _channels[idx].frequency;
    if (_burstMacroHz == 0) _burstMacroHz = 15;

    uint64_t periodUs = 1000000ULL / _burstMacroHz;

    if (_macroTimer != nullptr) {
        gptimer_stop(_macroTimer);
        gptimer_disable(_macroTimer);
        gptimer_del_timer(_macroTimer);
        _macroTimer = nullptr;
    }

    gptimer_config_t timer_cfg = {};
    timer_cfg.clk_src = GPTIMER_CLK_SRC_DEFAULT;
    timer_cfg.direction = GPTIMER_COUNT_UP;
    timer_cfg.resolution_hz = 1000000; // 1 MHz

    err = gptimer_new_timer(&timer_cfg, &_macroTimer);
    if (err != ESP_OK) {
        Serial.printf("[BURST] ERROR creando GPTimer: %d\n", (int)err);
        return;
    }

    gptimer_event_callbacks_t cbs = {};
    cbs.on_alarm = onMacroTimerAlarm;
    gptimer_register_event_callbacks(_macroTimer, &cbs, this);

    gptimer_alarm_config_t alarm_cfg = {};
    alarm_cfg.reload_count = 0;
    alarm_cfg.alarm_count = periodUs;
    alarm_cfg.flags.auto_reload_on_alarm = true;
    gptimer_set_alarm_action(_macroTimer, &alarm_cfg);

    gptimer_enable(_macroTimer);
    gptimer_start(_macroTimer);

    _burstModeActive = true;
    _burstTrigger = false;

    Serial.printf("[BURST] Timer armado: %lu Hz (periodo %llu us) | GPIO %d\n",
                  (unsigned long)_burstMacroHz, periodUs, PWM_PINS[idx].pin);
}

// stopBurstMode — Detiene Timer + RMT y LIBERA el GPIO para LEDC
void MagnetoController::stopBurstMode() {
    // 1. Detener timer macro
    if (_macroTimer != nullptr) {
        gptimer_stop(_macroTimer);
        gptimer_disable(_macroTimer);
        gptimer_del_timer(_macroTimer);
        _macroTimer = nullptr;
    }

    // 2. Destruir RMT completamente para liberar el GPIO
    if (_rmtChannel != nullptr) {
        rmt_disable(_rmtChannel);
        rmt_del_channel(_rmtChannel);
        _rmtChannel = nullptr;
    }
    if (_rmtEncoder != nullptr) {
        rmt_del_encoder(_rmtEncoder);
        _rmtEncoder = nullptr;
    }

    _burstTrigger = false;
    _burstModeActive = false;
    _burstMacroHz = 0;

    // 3. Reconfigurar LEDC en el pin (devolver control al PWM)
    ledc_channel_config_t cc = {};
    cc.gpio_num = PWM_PINS[0].pin;
    cc.speed_mode = PWM_SPEED_MODE;
    cc.channel = PWM_PINS[0].channel;
    cc.intr_type = LEDC_INTR_DISABLE;
    cc.timer_sel = PWM_PINS[0].timer;
    cc.duty = 0;
    cc.hpoint = 0;
    ledc_channel_config(&cc);

    Serial.println("[BURST] Modo burst detenido. GPIO devuelto a LEDC.");
}

// stopLedc — Detiene LEDC y libera el pin
void MagnetoController::stopLedc(int idx) {
    ledc_set_duty(PWM_SPEED_MODE, PWM_PINS[idx].channel, 0);
    ledc_update_duty(PWM_SPEED_MODE, PWM_PINS[idx].channel);
    ledc_stop(PWM_SPEED_MODE, PWM_PINS[idx].channel, 0);
}

// update — Llamar cada loop(). Gestiona tiempo de sesión + burst trigger
void MagnetoController::update() {
    updateSpwm();
    // Disparar ráfaga RMT si el timer ISR lo pidió
    if (_burstModeActive && _burstTrigger && _state == SystemState::RUNNING) {
        _burstTrigger = false;
        if (_rmtChannel != nullptr && _rmtEncoder != nullptr && _burstSymbolCount > 0) {
            rmt_transmit_config_t tx_cfg = {};
            tx_cfg.loop_count = 0; // single-shot
            rmt_transmit(_rmtChannel, _rmtEncoder, _burstSymbols,
                         _burstSymbolCount * sizeof(rmt_symbol_word_t), &tx_cfg);
        }
    }

    if (_state != SystemState::RUNNING) return;

    const int64_t nowUs = esp_timer_get_time();


    const uint64_t elapsedUs =
        (uint64_t)_elapsedBeforePauseUs + (uint64_t)(nowUs - _runStartUs);
    const unsigned long elapsedMs = (unsigned long)(elapsedUs / 1000ULL);

    if (elapsedMs >= _treatment.totalDurationMs) {
        Serial.println("[TIMER] Sesion completada por tiempo. Apagando (IEC 60601-2-10).");
        stopTreatment("completed");
        return;
    }

    _treatment.elapsedBeforePauseMs = elapsedMs;
    _treatment.remainingMs = _treatment.totalDurationMs - elapsedMs;
    _treatment.remainingMinutes = (int)(_treatment.remainingMs / 60000UL);
}

// Carga de preset clinico
bool MagnetoController::loadPreset(uint8_t presetId) {
    if (presetId >= PRESET_COUNT) {
        Serial.printf("[PRESET] ID %d invalido. Rango: 0-%d\n", presetId, PRESET_COUNT - 1);
        return false;
    }

    const TherapyPreset& p = PRESETS[presetId];
    ControlConfiguration proposed = getConfiguration();
    proposed.presetId = presetId;
    const uint16_t duty = pctToDuty16(p.dutyPct);

    for (int i = 0; i < PWM_CH_COUNT; i++) {
        proposed.channels[i].frequency = p.frequency;
        proposed.channels[i].duty = duty;
        proposed.channels[i].dutyMin = pctToDuty16(DUTY_MIN_PCT);
        proposed.channels[i].waveform = p.waveform;
        proposed.channels[i].phase = 0;
        proposed.channels[i].enabled = true;
        proposed.burstFrequency[i] = 0; // Ráfaga fija: 225 us + 25 us.

    }
    if (!applyConfiguration(proposed)) return false;

    Serial.printf("[PRESET] Cargado: %s | %lu Hz | %.2f%% duty | %d min\n",
                  p.name, p.frequency, p.dutyPct, p.sessionMinutes);
    Serial.printf("[PRESET] Evidencia: %s | Fuente: %s\n", p.evidence, p.source);
    return true;
}

// Control de sesion
bool MagnetoController::startTreatment(int durationMin, const ControlConfiguration* proposed) {
    const ControlConfiguration candidate = proposed ? *proposed : getConfiguration();
    if (_state != SystemState::IDLE || _treatment.isActive ||
        !validDuration(durationMin, candidate.presetId) ||
        validateConfiguration(candidate) != nullptr) return false;
    if (!applyConfiguration(candidate)) return false;

    ++_sessionId; _stopReason = "running";
    _treatment.totalDurationMs = (unsigned long)durationMin * 60UL * 1000UL;
    _treatment.startTime = millis();
    _treatment.elapsedBeforePauseMs = 0;
    _treatment.remainingMs = _treatment.totalDurationMs;
    _treatment.remainingMinutes = durationMin;
    _treatment.isActive = true;
    _runStartUs = esp_timer_get_time();
    _elapsedBeforePauseUs = 0;
    _state = SystemState::RUNNING;

    // Aplicar según tipo de onda
    applyAllChannels();

    Serial.printf("[SYSTEM] Tratamiento iniciado: %d min | Preset: %s\n",
                  durationMin, PRESETS[_activePresetId].name);
    return true;
}

bool MagnetoController::pauseTreatment() {
    if (_state != SystemState::RUNNING || !_treatment.isActive) {
        return false;
    }

    const int64_t nowUs = esp_timer_get_time();
    _elapsedBeforePauseUs += (nowUs - _runStartUs);
    _treatment.elapsedBeforePauseMs = (unsigned long)(_elapsedBeforePauseUs / 1000ULL);

    if (_treatment.elapsedBeforePauseMs >= _treatment.totalDurationMs) {
        _treatment.remainingMs = 0;
        _treatment.remainingMinutes = 0;
        stopTreatment("completed");
        return false;
    }

    _treatment.remainingMs = _treatment.totalDurationMs - _treatment.elapsedBeforePauseMs;
    _treatment.remainingMinutes = (int)(_treatment.remainingMs / 60000UL);
    _state = SystemState::PAUSED;

    // Detener salida
    stopSpwm();
    if (_burstModeActive) stopBurstMode();
        for (int i = 0; i < PWM_CH_COUNT; i++) stopLedc(i);

    Serial.printf("[SYSTEM] Tratamiento pausado. Restante: %lu s\n",
                  _treatment.remainingMs / 1000UL);
    return true;
}

bool MagnetoController::resumeTreatment(const ControlConfiguration* proposed) {
    if (_state != SystemState::PAUSED || !_treatment.isActive || _treatment.remainingMs == 0) {
        return false;
    }
    const ControlConfiguration candidate = proposed ? *proposed : getConfiguration();
    if (validateConfiguration(candidate) != nullptr) return false;
    if (!applyConfiguration(candidate)) return false;

    _runStartUs = esp_timer_get_time();
    _treatment.startTime = millis();
    _state = SystemState::RUNNING;

    applyAllChannels();

    Serial.printf("[SYSTEM] Tratamiento reanudado. Restante: %lu s\n",
                  _treatment.remainingMs / 1000UL);
    return true;
}

void MagnetoController::stopTreatment(const char* reason) {
    if (_treatment.isActive) {
        uint64_t elapsed = _elapsedBeforePauseUs;
        if (_state == SystemState::RUNNING) elapsed += esp_timer_get_time() - _runStartUs;
        const unsigned long elapsedMs = (unsigned long)(elapsed / 1000);
        _lastElapsedMs = elapsedMs < _treatment.totalDurationMs ? elapsedMs : _treatment.totalDurationMs;
        _lastTotalMs = _treatment.totalDurationMs;
        _stopReason = reason;
    }
    stopSpwm();
    if (_burstModeActive) stopBurstMode();
        for (int i = 0; i < PWM_CH_COUNT; i++) stopLedc(i);

    _treatment.isActive = false;
    _treatment.startTime = 0;
    _treatment.totalDurationMs = 0;
    _treatment.elapsedBeforePauseMs = 0;
    _treatment.remainingMs = 0;
    _treatment.remainingMinutes = 0;
    _runStartUs = 0;
    _elapsedBeforePauseUs = 0;
    _state = SystemState::IDLE;

    Serial.println("[SYSTEM] Tratamiento detenido. Bobinas en 0.");
}

// Validaciones
ControlConfiguration MagnetoController::getConfiguration() const {
    ControlConfiguration result;
    result.presetId = _activePresetId;
    for (int i = 0; i < PWM_CH_COUNT; ++i) {
        result.channels[i] = _channels[i];
        result.burstFrequency[i] = _burstFreq[i];
    }
    return result;
}

bool MagnetoController::validDuration(int durationMin, uint8_t presetId) {
    const int minimum = SESSION_MIN_MIN;
    return presetId < PRESET_COUNT && durationMin >= minimum &&
           durationMin <= SESSION_MAX_MIN && durationMin <= SESSION_SAFETY_MAX_MIN;
}

const char* MagnetoController::validateConfiguration(const ControlConfiguration& proposed) const {
    if (proposed.presetId >= PRESET_COUNT) return "id";
    for (int i = 0; i < PWM_CH_COUNT; ++i) {
        const MagnetoChannelParams& p = proposed.channels[i];
        if (p.frequency < FREQ_MIN_HZ || p.frequency > FREQ_MAX_HZ) return "freq";
        if (p.duty > pctToDuty16(DUTY_MAX_PCT)) return "duty";
        if (p.enabled && p.waveform != WaveShape::BURST && p.duty < pctToDuty16(DUTY_MIN_PCT)) return "duty";
        if (p.waveform == WaveShape::BURST && (p.frequency != 15 || proposed.burstFrequency[i] != 0)) return "burst_freq";
        switch (p.waveform) {
            case WaveShape::SQUARE:
            case WaveShape::PWM_PURE:
            case WaveShape::BURST:
                break;
            case WaveShape::TRIANGULAR:
            case WaveShape::TRAPEZOIDAL:
                return "wave"; // Ensayo: PWM rectangular y rafaga fija.
                break;
            default:
                return "wave";
        }
        // El desfase se implementa en LEDC directo, no en SPWM ni en BURST.
        if (p.phase < 0 || p.phase >= 360 ||
            (p.phase != 0 && p.waveform != WaveShape::SQUARE &&
             p.waveform != WaveShape::PWM_PURE)) return "phase";
        const uint32_t hz = proposed.burstFrequency[i];
        const uint32_t period = hz == 0
            ? BURST_PULSE_WIDTH_US + BURST_GAP_US
            : 1000000UL / hz;
        if (period == 0) return "burst_freq";
        const uint32_t high = hz == 0 ? BURST_PULSE_WIDTH_US : period * 9 / 10;
        const uint32_t low = period - high;
        const uint32_t window = BURST_WINDOW_MS * 1000UL;
        const uint32_t symbols = window / period;
        if (high == 0 || low == 0 || high > 32767 || low > 32767 ||
            symbols == 0 || symbols > BURST_MAX_SYMBOLS) return "burst_freq";
        if (p.waveform == WaveShape::BURST &&
            window > 1000000UL / p.frequency) return "freq";
    }
    return nullptr;
}

bool MagnetoController::applyConfiguration(const ControlConfiguration& proposed) {
    // Ningún canal ni periférico cambia antes de aprobar TODOS los parámetros.
    if (validateConfiguration(proposed) != nullptr) return false;
    const bool running = _state == SystemState::RUNNING;
    stopSpwm();
    if (_burstModeActive || _rmtChannel != nullptr || _macroTimer != nullptr) {
        stopBurstMode();
    }
    for (int i = 0; i < PWM_CH_COUNT; ++i) stopLedc(i);
    for (int i = 0; i < PWM_CH_COUNT; ++i) {
        _channels[i] = proposed.channels[i];
        _burstFreq[i] = proposed.burstFrequency[i];
    }
    _activePresetId = proposed.presetId;
    if (running) applyAllChannels();
    return true;
}

bool MagnetoController::validateFreq(uint32_t freq) const {
    if (freq < FREQ_MIN_HZ || freq > FREQ_MAX_HZ) {
        Serial.printf("[SAFETY] Frecuencia %lu Hz fuera del rango ELF [%d-%d Hz].\n",
                      freq, FREQ_MIN_HZ, FREQ_MAX_HZ);
        return false;
    }
    return true;
}

bool MagnetoController::validateDuty(uint16_t duty) const {
    const float pct = duty16ToPct(duty);
    if (duty > pctToDuty16(DUTY_MAX_PCT)) {
        Serial.printf("[SAFETY] Duty %.1f%% supera limite de %.1f%%.\n", pct, DUTY_MAX_PCT);
        return false;
    }
    return true;
}

// Setters con hot-swap
void MagnetoController::setChannelFreq(int ch, uint32_t freq) {
    if (ch < -1 || ch >= PWM_CH_COUNT || !validateFreq(freq)) return;
    ControlConfiguration proposed = getConfiguration();
    for (int i = 0; i < PWM_CH_COUNT; i++) {
        if (ch != -1 && i != ch) continue;
        proposed.channels[i].frequency = freq;
    }
    proposed.presetId = PRESET_CUSTOM;
    applyConfiguration(proposed);
}

void MagnetoController::setChannelDuty(int ch, uint16_t duty) {
    if (ch < -1 || ch >= PWM_CH_COUNT || !validateDuty(duty)) return;
    ControlConfiguration proposed = getConfiguration();
    for (int i = 0; i < PWM_CH_COUNT; i++) {
        if (ch != -1 && i != ch) continue;
        proposed.channels[i].duty = duty;
    }
    proposed.presetId = PRESET_CUSTOM;
    applyConfiguration(proposed);
}

void MagnetoController::setChannelDutyPct(int ch, float pct) {
    if (!isfinite(pct) || pct < 0 || pct > DUTY_MAX_PCT) return;
    setChannelDuty(ch, pctToDuty16(pct));
}

void MagnetoController::setChannelWaveform(int ch, WaveShape wave) {
    if (ch < -1 || ch >= PWM_CH_COUNT) return;
    ControlConfiguration proposed = getConfiguration();
    for (int i = 0; i < PWM_CH_COUNT; i++) {
        if (ch != -1 && i != ch) continue;
        proposed.channels[i].waveform = wave;
    }
    applyConfiguration(proposed);
}

void MagnetoController::setChannelPhase(int ch, int16_t phase) {
    if (ch < -1 || ch >= PWM_CH_COUNT || phase < 0 || phase > 360) return;
    ControlConfiguration proposed = getConfiguration();
    for (int i = 0; i < PWM_CH_COUNT; i++) {
        if (ch != -1 && i != ch) continue;
        proposed.channels[i].phase = phase % 360;
    }
    applyConfiguration(proposed);
}

void MagnetoController::setChannelEnabled(int ch, bool enabled) {
    if (ch < -1 || ch >= PWM_CH_COUNT) return;
    ControlConfiguration proposed = getConfiguration();
    for (int i = 0; i < PWM_CH_COUNT; i++) {
        if (ch != -1 && i != ch) continue;
        proposed.channels[i].enabled = enabled;
    }
    applyConfiguration(proposed);
}

void MagnetoController::setBurstFrequency(int ch, uint32_t burstHz) {
    if (ch < -1 || ch >= PWM_CH_COUNT) return;
    ControlConfiguration proposed = getConfiguration();
    for (int i = 0; i < PWM_CH_COUNT; i++) {
        if (ch != -1 && i != ch) continue;
        proposed.burstFrequency[i] = burstHz;
    }
    applyConfiguration(proposed);
}

// updateSpwm — Llamada desde update()/loop(). Calcula fase dentro del ciclo
// terapéutico usando el reloj de alta precisión y actualiza el duty LEDC.
// Corre en CPU 1 (loopTask), NO bloquea BLE ni watchdog de CPU 0.
void MagnetoController::updateSpwm() {
    if (!_spwmActive || _state != SystemState::RUNNING) return;

    // Posición actual dentro del ciclo terapéutico (ej. 0-13333 µs a 75Hz)
    uint32_t nowUs = (uint32_t)(esp_timer_get_time() & 0xFFFFFFFFUL);
    uint32_t phase = nowUs % _spwmPeriodUs;

    // Guardia de seguridad: si dutyMax <= dutyMin (usuario configuró intensity < 5%),
    // la resta uint16_t haría wrap-around a valores altísimos. Forzar duty fijo.
    if (_spwmDutyMax <= _spwmDutyMin) {
        ledc_set_duty(PWM_SPEED_MODE, PWM_PINS[_spwmChannelIdx].channel, _spwmDutyMin);
        ledc_update_duty(PWM_SPEED_MODE, PWM_PINS[_spwmChannelIdx].channel);
        return;
    }

    uint16_t range = _spwmDutyMax - _spwmDutyMin;
    uint16_t duty  = _spwmDutyMin;

    if (_spwmShape == WaveShape::TRIANGULAR) {
        if (phase < _spwmHalfPeriodUs) {
            duty = _spwmDutyMin
                 + (uint16_t)((uint64_t)range * phase / _spwmHalfPeriodUs);
        } else {
            uint32_t downPhase = phase - _spwmHalfPeriodUs;
            duty = _spwmDutyMax
                 - (uint16_t)((uint64_t)range * downPhase / _spwmHalfPeriodUs);
        }
    } else if (_spwmShape == WaveShape::TRAPEZOIDAL) {
        if (phase < _spwmQ1Us) {
            duty = _spwmDutyMin
                 + (uint16_t)((uint64_t)range * phase / _spwmQ1Us);
        } else if (phase < _spwmQ3Us) {
            duty = _spwmDutyMax;
        } else {
            uint32_t downPhase = phase - _spwmQ3Us;
            duty = _spwmDutyMax
                 - (uint16_t)((uint64_t)range * downPhase / _spwmQ1Us);
        }
    }

    ledc_set_duty(PWM_SPEED_MODE, PWM_PINS[_spwmChannelIdx].channel, duty);
    ledc_update_duty(PWM_SPEED_MODE, PWM_PINS[_spwmChannelIdx].channel);
}

// startSpwm — Reconfigura LEDC a 10kHz/12-bit y arranca el timer periódico
void MagnetoController::startSpwm(int idx) {
    stopSpwm();

    _spwmShape      = _channels[idx].waveform;
    _spwmChannelIdx = idx;
    _spwmPhaseUs    = 0;

    // Escalar duty de 16-bit (0-65535) a 12-bit (0-4095)
    _spwmDutyMin    = (uint16_t)((uint32_t)_channels[idx].dutyMin * SPWM_MAX_DUTY / PWM_MAX_DUTY);
    _spwmDutyMax    = (uint16_t)((uint32_t)_channels[idx].duty    * SPWM_MAX_DUTY / PWM_MAX_DUTY);

    // Calcular tiempos del ciclo terapéutico
    uint32_t therapyHz = _channels[idx].frequency;
    if (therapyHz == 0) therapyHz = 75;
    _spwmPeriodUs     = 1000000UL / therapyHz;
    _spwmHalfPeriodUs = _spwmPeriodUs / 2;
    _spwmQ1Us         = _spwmPeriodUs / 4;
    _spwmQ3Us         = _spwmQ1Us * 3;

    // Reconfigurar timer LEDC: 12-bit / 10 kHz (el ESP32 no soporta 16-bit a >1220 Hz)
    const ledc_timer_t timer = PWM_PINS[idx].timer;
    ledc_timer_config_t tc = {};
    tc.speed_mode      = PWM_SPEED_MODE;
    tc.duty_resolution = SPWM_RESOLUTION;   // 12 bits = 4095 max
    tc.timer_num       = timer;
    tc.freq_hz         = SPWM_CARRIER_HZ;   // 10 kHz
    tc.clk_cfg         = LEDC_AUTO_CLK;
    ledc_timer_config(&tc);

    // Arrancar con duty mínimo
    ledc_set_duty(PWM_SPEED_MODE, PWM_PINS[idx].channel, _spwmDutyMin);
    ledc_update_duty(PWM_SPEED_MODE, PWM_PINS[idx].channel);

    _spwmActive = true;

    Serial.printf("[SPWM] %s | carrier=%uHz/12bit | therapy=%luHz | period=%luus | duty12=%u-%u (%.1f%%-%.1f%%)\n",
                  _spwmShape == WaveShape::TRIANGULAR ? "TRIANGULAR" : "TRAPEZOIDAL",
                  SPWM_CARRIER_HZ, (unsigned long)therapyHz,
                  (unsigned long)_spwmPeriodUs,
                  _spwmDutyMin, _spwmDutyMax,
                  duty16ToPct(_channels[idx].dutyMin), duty16ToPct(_channels[idx].duty));
}

// stopSpwm — Detiene el timer SPWM y restaura LEDC a 16-bit
void MagnetoController::stopSpwm() {
    if (!_spwmActive) return;

    // Poner duty a 0 antes de reconfigurar
    ledc_set_duty(PWM_SPEED_MODE, PWM_PINS[_spwmChannelIdx].channel, 0);
    ledc_update_duty(PWM_SPEED_MODE, PWM_PINS[_spwmChannelIdx].channel);

    // Restaurar timer LEDC a 16-bit para modos SQUARE/PWM_PURE/BURST
    if (_spwmChannelIdx >= 0 && _spwmChannelIdx < PWM_CH_COUNT) {
        uint32_t restoreHz = _channels[_spwmChannelIdx].frequency;
        if (restoreHz == 0 || restoreHz > 1000) restoreHz = 75;
        ledc_timer_config_t tc = {};
        tc.speed_mode      = PWM_SPEED_MODE;
        tc.duty_resolution = PWM_RESOLUTION;     // 16 bits
        tc.timer_num       = PWM_PINS[_spwmChannelIdx].timer;
        tc.freq_hz         = restoreHz;
        tc.clk_cfg         = LEDC_AUTO_CLK;
        ledc_timer_config(&tc);
    }

    _spwmActive  = false;
    _spwmPhaseUs = 0;
    Serial.println("[SPWM] Detenido. LEDC restaurado a 16-bit.");
}

// applyPwm — Todas las ondas salen por LEDC en GPIO04.
//   TRIANGULAR/TRAPEZOIDAL: SPWM a 20kHz modulado por timer.
//   SQUARE/PWM_PURE: LEDC a frecuencia terapéutica con duty fijo.
void MagnetoController::applyPwm(int idx) {
    if (!_channels[idx].enabled) return;
    if (_channels[idx].waveform == WaveShape::BURST) return;

    stopSpwm();
    stopLedc(idx);

    if (_channels[idx].waveform == WaveShape::TRIANGULAR || _channels[idx].waveform == WaveShape::TRAPEZOIDAL) {
        // Modo SPWM: carrier a 20kHz, duty modulado por timer cada 100µs
        startSpwm(idx);
    } else {
        // SQUARE / PWM_PURE: LEDC a la frecuencia terapéutica exacta con duty fijo
        const ledc_timer_t timer = PWM_PINS[idx].timer;
        const esp_err_t freqErr = ledc_set_freq(PWM_SPEED_MODE, timer, _channels[idx].frequency);
        const uint32_t actualHz = ledc_get_freq(PWM_SPEED_MODE, timer);
        const uint32_t hpoint = (uint32_t)((uint64_t)PWM_MAX_DUTY * _channels[idx].phase / 360);
        ledc_set_duty_and_update(PWM_SPEED_MODE, PWM_PINS[idx].channel, _channels[idx].duty, hpoint);

        if (freqErr != ESP_OK || actualHz != _channels[idx].frequency) {
            Serial.printf("[PWM] CH%d freq solicitada=%lu Hz | real=%lu Hz | err=%d\n",
                          idx, _channels[idx].frequency, actualHz, (int)freqErr);
        }

        Serial.printf("[PWM] CH%d -> %lu Hz | duty=%u (%.2f%%) | wave=%d\n",
                      idx, _channels[idx].frequency,
                      _channels[idx].duty,
                      duty16ToPct(_channels[idx].duty),
                      (int)_channels[idx].waveform);
    }
}

void MagnetoController::applyAllChannels() {
    for (int i = 0; i < PWM_CH_COUNT; i++) {
        if (!_channels[i].enabled) continue;
        if (_channels[i].waveform == WaveShape::BURST) {
            startBurstMode(i);
        } else {
            applyPwm(i);
        }
    }
}

// Getters y status
TreatmentStatus MagnetoController::getTreatmentStatus() const {
    return _treatment;
}

MagnetoChannelParams MagnetoController::getChannelParams(int idx) const {
    if (idx >= 0 && idx < PWM_CH_COUNT) return _channels[idx];
    return {0, 0, 0, WaveShape::SQUARE, 0, false};
}

String MagnetoController::buildStatusString() const {
    String s = "{\"state\":";
    switch (_state) {
        case SystemState::RUNNING:     s += "\"RUNNING\""; break;
        case SystemState::PAUSED:      s += "\"PAUSED\""; break;
        case SystemState::LOW_BATTERY: s += "\"LOW_BAT\""; break;
        case SystemState::ERROR:       s += "\"ERROR\""; break;
        default:                       s += "\"IDLE\""; break;
    }

    s += ",\"session_id\":" + String(_sessionId);
    s += ",\"stop_reason\":\"" + String(_stopReason) + "\"";
    s += ",\"total_sec\":" + String((_treatment.isActive ? _treatment.totalDurationMs : _lastTotalMs)/1000UL);
    s += ",\"elapsed_sec\":" + String((_treatment.isActive ? _treatment.totalDurationMs-_treatment.remainingMs : _lastElapsedMs)/1000UL);
    s += ",\"preset\":" + String(_activePresetId);
    s += ",\"preset_name\":\"" + String(PRESETS[_activePresetId].name) + "\"";
    s += ",\"remaining_sec\":" + String(_treatment.remainingMs / 1000UL);
    s += ",\"remaining_min\":" + String(_treatment.remainingMinutes);
    s += ",\"burst_active\":" + String(_burstModeActive ? "true" : "false");
    s += ",\"channels\":[";
    for (int i = 0; i < PWM_CH_COUNT; i++) {
        if (i > 0) s += ",";
        s += "{\"ch\":" + String(i)
           + ",\"freq\":" + String(_channels[i].frequency)
           + ",\"duty_pct\":" + String(duty16ToPct(_channels[i].duty), 1)
           + ",\"wave\":" + String((int)_channels[i].waveform)
           + ",\"phase\":" + String(_channels[i].phase)
           + ",\"en\":" + (_channels[i].enabled ? "true" : "false");
        if (_channels[i].waveform == WaveShape::BURST) {
            s += ",\"burst_hz\":" + String(_burstFreq[i] > 0 ? _burstFreq[i] : 0);
            s += ",\"burst_pulses\":" + String(_burstSymbolCount);
        }
        s += "}";
    }
    s += "]}";
    return s;
}
