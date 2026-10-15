/** Interpretacion atomica de ordenes JSON del protocolo BLE version 1. */

#include "CommandHandler.h"
#include <math.h>

CommandHandler::CommandHandler(MagnetoController& ctrl, BatteryMonitor& battery, BLEManager& ble, StorageManager& storage)
    : _ctrl(ctrl), _battery(battery), _ble(ble), _storage(storage) {}

void CommandHandler::handle(const String& rawJson) {
    StaticJsonDocument<JSON_BUFFER_SIZE> doc;
    DeserializationError err = deserializeJson(doc, rawJson.c_str());
    if (err) {
        String msg = "{\"error\":\"JSON invalido:" + String(err.c_str()) + "\"}";
        Serial.println("[CMD] " + msg);
        _ble.sendNotification(msg);
        return;
    }

    if (!doc.is<JsonObject>() || !doc["cmd"].is<const char*>()) {
        reject("cmd", "INVALID_COMMAND");
        return;
    }
    const char* cmd = doc["cmd"];
    Serial.println("\n╔══════════════════════════════════════════╗");
    Serial.printf(  "║  [BLE CMD] %-30s  ║\n", cmd);
    Serial.println("╚══════════════════════════════════════════╝");
    Serial.printf("[CMD] RAW: %s\n", rawJson.c_str());

    if (strcmp(cmd, CMD_START) == 0 || strcmp(cmd, CMD_RESUME) == 0 || strcmp(cmd, CMD_SET) == 0) {
        const bool fieldsOk = strcmp(cmd, CMD_START) == 0
            ? allowedFields(doc, {"cmd", "ch", "freq", "duty", "intensity", "wave", "phase", "en", "burst_freq", "duration"})
            : allowedFields(doc, {"cmd", "ch", "freq", "duty", "intensity", "wave", "phase", "en", "burst_freq"});
        if (!fieldsOk) return;
        if (strcmp(cmd, CMD_START) == 0) processStart(doc);
        else if (strcmp(cmd, CMD_RESUME) == 0) processResume(doc);
        else processSet(doc);
    } else if (strcmp(cmd, CMD_PRESET) == 0) {
        if (allowedFields(doc, {"cmd", "id"})) processPreset(doc);
    } else if (strcmp(cmd, CMD_PAUSE) == 0 || strcmp(cmd, CMD_STOP) == 0 || strcmp(cmd, CMD_STATUS) == 0) {
        if (!allowedFields(doc, {"cmd"})) return;
        if (strcmp(cmd, CMD_PAUSE) == 0) processPause();
        else if (strcmp(cmd, CMD_STOP) == 0) processStop();
        else processStatus();
    } else {
        reject("cmd", "UNKNOWN_COMMAND");
    }
}

// CMD: preset — Carga un protocolo clínico completo de una sola vez
void CommandHandler::processPreset(const JsonDocument& doc) {
    if (!doc["id"].is<int>() || doc["id"].as<int>() < 0 || doc["id"].as<int>() >= PRESET_COUNT) {
        reject("id");
        return;
    }
    if (_ctrl.getState() == SystemState::RUNNING && _battery.isEmpty()) {
        reject("battery", "LOW_BAT");
        return;
    }
    uint8_t pid = (uint8_t)doc["id"].as<int>();
    if (!_ctrl.loadPreset(pid)) {
        _ble.sendNotification("{\"error\":\"preset id invalido\"}");
        return;
    }
    // Guardar selección en NVS
    _storage.saveLastDuration(PRESETS[pid].sessionMinutes);

    Serial.println("┌──────────────────────────────────────────┐");
    Serial.printf( "│ PRESET CARGADO: %-24s │\n", PRESETS[pid].name);
    Serial.printf( "│ Frecuencia:    %-4lu Hz                   │\n", PRESETS[pid].frequency);
    Serial.printf( "│ Intensidad:    %-6.2f %%                  │\n", PRESETS[pid].dutyPct);
    Serial.printf( "│ Sesión:        %-4d min                  │\n", PRESETS[pid].sessionMinutes);
    Serial.printf( "│ Evidencia:     %-24s │\n", PRESETS[pid].evidence);
    Serial.println("└──────────────────────────────────────────┘");

    String resp = "{\"ok\":\"preset\",\"id\":" + String(pid)
                + ",\"name\":\"" + String(PRESETS[pid].name) + "\""
                + ",\"freq\":" + String(PRESETS[pid].frequency)
                + ",\"duty_pct\":" + String(PRESETS[pid].dutyPct, 2)
                + ",\"session_min\":" + String(PRESETS[pid].sessionMinutes) + "}";
    _ble.sendNotification(resp);
}

// CMD: start — Aplicar parámetros opcionales y arrancar sesión
void CommandHandler::processStart(const JsonDocument& doc) {
    if (_ctrl.getState() != SystemState::IDLE) {
        reject("state", "NOT_IDLE");
        return;
    }
    if (_battery.isEmpty()) {
        String msg = "{\"error\":\"LOW_BAT\",\"action\":\"start_blocked\",\"bat\":"
                   + String(_battery.getPercentage())
                   + ",\"v\":" + String(_battery.getVoltage(), 2) + "}";
        _ble.sendNotification(msg);
        return;
    }
    ControlConfiguration proposed;
    bool changed;
    if (!prepareConfiguration(doc, proposed, changed)) return;
    if (doc.containsKey("duration") && !doc["duration"].is<int>()) {
        reject("duration");
        return;
    }
    const int duration = doc.containsKey("duration") ? doc["duration"].as<int>()
        : PRESETS[proposed.presetId].sessionMinutes;
    if (!MagnetoController::validDuration(duration, proposed.presetId)) {
        reject("duration");
        return;
    }
    if (!_ctrl.startTreatment(duration, &proposed)) {
        reject("state", "START_FAILED");
        return;
    }
    if (changed) saveConfiguration();
    _storage.saveLastDuration(duration);

    MagnetoChannelParams p = _ctrl.getChannelParams(0);
    Serial.println("\n▶▶▶ TRATAMIENTO INICIADO ◀◀◀");
    Serial.printf("    Duración:    %d min\n", duration);
    Serial.printf("    Frecuencia:  %lu Hz\n", p.frequency);
    Serial.printf("    Intensidad:  %.2f %%\n", duty16ToPct(p.duty));
    Serial.printf("    GPIO:        %d\n", PWM_PINS[0].pin);
    Serial.println("─────────────────────────────");

    _ble.sendNotification("{\"ok\":\"started\",\"duration\":" + String(duration) + "}");
}

// CMD: pause
void CommandHandler::processPause() {
    if (_ctrl.getState() != SystemState::RUNNING || !_ctrl.pauseTreatment()) {
        _ble.sendNotification("{\"error\":\"NOT_RUNNING\",\"action\":\"pause_failed\"}");
        return;
    }

    TreatmentStatus t = _ctrl.getTreatmentStatus();
    MagnetoChannelParams p = _ctrl.getChannelParams(0);
    String resp = "{\"ok\":\"paused\",\"remaining_sec\":" + String(t.remainingMs / 1000UL)
                + ",\"freq\":" + String(p.frequency)
                + ",\"intensity\":" + String(duty16ToPct(p.duty), 2) + "}";
    _ble.sendNotification(resp);
}

// CMD: resume
void CommandHandler::processResume(const JsonDocument& doc) {
    if (_battery.isEmpty()) {
        String msg = "{\"error\":\"LOW_BAT\",\"action\":\"resume_blocked\",\"bat\":"
                   + String(_battery.getPercentage())
                   + ",\"v\":" + String(_battery.getVoltage(), 2) + "}";
        _ble.sendNotification(msg);
        return;
    }
    if (_ctrl.getState() != SystemState::PAUSED) {
        _ble.sendNotification("{\"error\":\"NOT_PAUSED\",\"action\":\"resume_failed\"}");
        return;
    }

    ControlConfiguration proposed;
    bool changed;
    if (!prepareConfiguration(doc, proposed, changed)) return;
    if (!_ctrl.resumeTreatment(&proposed)) {
        _ble.sendNotification("{\"error\":\"RESUME_FAILED\",\"action\":\"resume_failed\"}");
        return;
    }
    if (changed) saveConfiguration();

    TreatmentStatus t = _ctrl.getTreatmentStatus();
    MagnetoChannelParams p = _ctrl.getChannelParams(0);
    String resp = "{\"ok\":\"resumed\",\"remaining_sec\":" + String(t.remainingMs / 1000UL)
                + ",\"freq\":" + String(p.frequency)
                + ",\"intensity\":" + String(duty16ToPct(p.duty), 2) + "}";
    _ble.sendNotification(resp);
}

// CMD: stop
void CommandHandler::processStop() {
    _ctrl.stopTreatment();
    Serial.println("\n■■■ TRATAMIENTO DETENIDO ■■■");
    Serial.println("─────────────────────────────");
    JsonDocument response;
    deserializeJson(response, _ctrl.buildStatusString().c_str());
    response["ok"] = "stopped";
    char payload[JSON_BUFFER_SIZE];
    serializeJson(response, payload, sizeof(payload));
    _ble.sendNotification(String(payload));
}

// CMD: set — Actualiza SOLO los campos presentes. Inteligente y seguro.
// Si el tratamiento está corriendo, los cambios se aplican en caliente.
void CommandHandler::processSet(const JsonDocument& doc) {
    ControlConfiguration proposed;
    bool changed;
    if (!prepareConfiguration(doc, proposed, changed)) return;
    if (_ctrl.getState() == SystemState::RUNNING && _battery.isEmpty()) {
        reject("battery", "LOW_BAT");
        return;
    }
    if (changed) {
        if (!_ctrl.applyConfiguration(proposed)) {
            reject("configuration");
            return;
        }
        saveConfiguration();
    }
    const int ch = doc.containsKey("ch") ? doc["ch"].as<int>() : -1;
    _ble.sendNotification("{\"ok\":\"set\",\"ch\":" + String(ch) + "}");
}

void CommandHandler::reject(const char* field, const char* error) {
    JsonDocument response;
    response["error"] = error;
    response["field"] = field;
    char payload[JSON_BUFFER_SIZE];
    serializeJson(response, payload, sizeof(payload));
    _ble.sendNotification(String(payload));
}

bool CommandHandler::allowedFields(const JsonDocument& doc, std::initializer_list<const char*> fields) {
    for (JsonPairConst entry : doc.as<JsonObjectConst>()) {
        bool known = false;
        for (const char* name : fields) {
            if (strcmp(entry.key().c_str(), name) == 0) { known = true; break; }
        }
        if (!known) {
            reject(entry.key().c_str(), "UNSUPPORTED_FIELD");
            return false;
        }
    }
    return true;
}

bool CommandHandler::prepareConfiguration(const JsonDocument& doc, ControlConfiguration& proposed, bool& changed) {
    proposed = _ctrl.getConfiguration();
    changed = false;
    int ch = -1;
    if (doc.containsKey("ch")) {
        if (!doc["ch"].is<int>() || doc["ch"].as<int>() < -1 || doc["ch"].as<int>() >= PWM_CH_COUNT) {
            reject("ch"); return false;
        }
        ch = doc["ch"].as<int>();
    }
    // Validar cada campo antes de convertirlo a un entero pequeño o aplicarlo.
    for (const char* key : {"freq", "duty", "wave", "phase", "burst_freq"}) {
        if (doc.containsKey(key) && !doc[key].is<int>()) { reject(key); return false; }
    }
    if (doc.containsKey("freq") && (doc["freq"].as<int>() < FREQ_MIN_HZ || doc["freq"].as<int>() > FREQ_MAX_HZ)) {
        reject("freq"); return false;
    }
    if (doc.containsKey("duty") && (doc["duty"].as<int>() < 0 ||
        doc["duty"].as<int>() > (int)PWM_MAX_DUTY ||
        doc["duty"].as<int>() > (int)pctToDuty16(DUTY_MAX_PCT))) {
        reject("duty"); return false;
    }
    if (doc.containsKey("intensity")) {
        const double pct = doc["intensity"].as<double>();
        if (!doc["intensity"].is<double>() || !isfinite(pct) || pct < 0 || pct > DUTY_MAX_PCT) {
            reject("intensity"); return false;
        }
    }
    if (doc.containsKey("duty") && doc.containsKey("intensity") &&
        doc["duty"].as<uint16_t>() != pctToDuty16(doc["intensity"].as<float>())) {
        reject("duty/intensity", "CONFLICTING_FIELDS"); return false;
    }
    if (doc.containsKey("wave") && (doc["wave"].as<int>() < WAVE_SQUARE || doc["wave"].as<int>() > WAVE_PWM_PURE)) {
        reject("wave"); return false;
    }
    if (doc.containsKey("phase") && (doc["phase"].as<int>() < 0 || doc["phase"].as<int>() > 360)) {
        reject("phase"); return false;
    }
    if (doc.containsKey("burst_freq") && doc["burst_freq"].as<int>() < 0) {
        reject("burst_freq"); return false;
    }
    if (doc.containsKey("en") && !doc["en"].is<bool>()) { reject("en"); return false; }
    for (const char* key : {"freq", "duty", "intensity", "wave", "phase", "en", "burst_freq"}) {
        changed = changed || doc.containsKey(key);
    }
    for (int i = 0; i < PWM_CH_COUNT; ++i) {
        if (ch != -1 && ch != i) continue;
        MagnetoChannelParams& p = proposed.channels[i];
        if (doc.containsKey("freq")) p.frequency = doc["freq"].as<uint32_t>();
        if (doc.containsKey("duty")) p.duty = doc["duty"].as<uint16_t>();
        else if (doc.containsKey("intensity")) p.duty = pctToDuty16(doc["intensity"].as<float>());
        if (doc.containsKey("wave")) p.waveform = (WaveShape)doc["wave"].as<int>();
        if (p.waveform == WaveShape::BURST) p.duty = pctToDuty16(DUTY_BURST_5MS_PCT);
        if (doc.containsKey("phase")) p.phase = doc["phase"].as<int>() % 360;
        if (doc.containsKey("en")) p.enabled = doc["en"].as<bool>();
        if (doc.containsKey("burst_freq")) proposed.burstFrequency[i] = doc["burst_freq"].as<uint32_t>();
    }
    if (changed) proposed.presetId = PRESET_CUSTOM;
    if (const char* field = _ctrl.validateConfiguration(proposed)) {
        reject(field); return false;
    }
    return true;
}

void CommandHandler::saveConfiguration() {
    for (int i = 0; i < PWM_CH_COUNT; ++i) _storage.saveParams(i, _ctrl.getChannelParams(i));
}

// CMD: status — Devuelve el estado completo del sistema en JSON
void CommandHandler::processStatus() {
    _ble.sendNotification(_ctrl.buildStatusString());
    _ble.sendNotification("{\"bat\":" + String(_battery.getPercentage()) + ",\"v\":" + String(_battery.getVoltage(),2) + "}");
}
