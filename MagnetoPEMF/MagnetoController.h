#ifndef MAGNETO_CONTROLLER_H
#define MAGNETO_CONTROLLER_H

#include "Config.h"
#include "MagnetoTypes.h"
#include "TherapyPresets.h"
#include <esp_timer.h>

class MagnetoController {
public:
    MagnetoController();
    void begin();
    void update(); // Llamar en cada loop()

    // --- Control de sesión ---
    bool startTreatment(int durationMin, const ControlConfiguration* proposed = nullptr);
    bool pauseTreatment();
    bool resumeTreatment(const ControlConfiguration* proposed = nullptr);
    void stopTreatment(const char* reason = "manual");

    // --- Carga de preset clínico ---
    bool loadPreset(uint8_t presetId);

    // Validación sin efectos y aplicación de la configuración completa.
    ControlConfiguration getConfiguration() const;
    const char* validateConfiguration(const ControlConfiguration& proposed) const;
    static bool validDuration(int durationMin, uint8_t presetId);
    bool applyConfiguration(const ControlConfiguration& proposed);

    // --- Setters granulares (hot-swap, ch==-1 aplica a todos) ---
    void setChannelFreq(int ch, uint32_t freq);
    void setChannelDuty(int ch, uint16_t duty);
    void setChannelDutyPct(int ch, float pct);
    void setChannelWaveform(int ch, WaveShape wave);
    void setChannelPhase(int ch, int16_t phase);
    void setChannelEnabled(int ch, bool enabled);
    void setBurstFrequency(int ch, uint32_t burstHz);

    // --- Getters ---
    SystemState          getState()        const { return _state; }
    TreatmentStatus      getTreatmentStatus() const;
    MagnetoChannelParams getChannelParams(int idx) const;
    uint8_t              getActivePreset() const { return _activePresetId; }

    // --- Status string (JSON) para BLE ---
    String buildStatusString() const;

private:
    // --- LEDC (ondas SQUARE / PWM_PURE) ---
    void applyPwm(int idx);
    void applyAllChannels();
    void stopLedc(int idx);
    bool validateFreq(uint32_t freq) const;
    bool validateDuty(uint16_t duty) const;

    // --- RMT + GPTimer (ondas BURST) ---
    void initRmt();
    void buildBurstPattern(int idx);
    void startBurstMode(int idx);
    void stopBurstMode();
    static bool IRAM_ATTR onMacroTimerAlarm(gptimer_handle_t timer,
                                             const gptimer_alarm_event_data_t *edata,
                                             void *user_data);

    // --- SPWM engine (Triangular / Trapezoidal real) ---
    void startSpwm(int idx);
    void stopSpwm();
    void updateSpwm();

    // --- Estado ---
    SystemState          _state;
    MagnetoChannelParams _channels[PWM_CH_COUNT];
    TreatmentStatus      _treatment;
    uint8_t              _activePresetId;
    const char* _stopReason = "boot";
    unsigned long _lastElapsedMs = 0, _lastTotalMs = 0;
    uint32_t _sessionId = 0;
    int64_t              _runStartUs;
    int64_t              _elapsedBeforePauseUs;

    // --- Burst engine (GPTimer + RMT) ---
    rmt_channel_handle_t _rmtChannel;
    rmt_encoder_handle_t _rmtEncoder;
    gptimer_handle_t     _macroTimer;
    rmt_symbol_word_t    _burstSymbols[BURST_MAX_SYMBOLS];
    size_t               _burstSymbolCount;
    volatile bool        _burstTrigger;
    bool                 _burstModeActive;
    uint32_t             _burstMacroHz;
    uint32_t             _burstFreq[PWM_CH_COUNT];

    // --- SPWM engine state ---
    bool                 _spwmActive;
    uint32_t             _spwmPhaseUs;
    uint32_t             _spwmPeriodUs;
    uint32_t             _spwmHalfPeriodUs;
    uint32_t             _spwmQ1Us;
    uint32_t             _spwmQ3Us;
    uint16_t             _spwmDutyMin;
    uint16_t             _spwmDutyMax;
    WaveShape            _spwmShape;
    int                  _spwmChannelIdx;
};

#endif // MAGNETO_CONTROLLER_H
