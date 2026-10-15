#include "StorageManager.h"

StorageManager::StorageManager() {}

void StorageManager::begin() {
    _prefs.begin("magneto", false);
    Serial.println("[STORAGE] NVS inicializado.");
}

void StorageManager::saveParams(int idx, const MagnetoChannelParams& params) {
    String key = "ch" + String(idx);
    _prefs.putUInt((key + "f").c_str(), params.frequency);
    _prefs.putUShort((key + "d").c_str(), params.duty);
    Serial.printf("[STORAGE] Parametros CH%d guardados.\n", idx);
}

void StorageManager::loadParams(int idx, MagnetoChannelParams& params) {
    String key = "ch" + String(idx);
    params.frequency = _prefs.getUInt((key + "f").c_str(), 20); // Default 20Hz
    params.duty = _prefs.getUShort((key + "d").c_str(), 0);     // Default 0 duty
    Serial.printf("[STORAGE] Parametros CH%d cargados: %luHz, %u\n", idx, params.frequency, params.duty);
}

void StorageManager::saveLastDuration(int durationMin) {
    _prefs.putInt("dur", durationMin);
}

int StorageManager::loadLastDuration() {
    return _prefs.getInt("dur", 20); // Default 20 min
}
