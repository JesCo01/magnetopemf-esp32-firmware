#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

#include <ArduinoJson.h>
#include <initializer_list>
#include "Config.h"
#include "BatteryMonitor.h"
#include "MagnetoController.h"
#include "BLEManager.h"
#include "StorageManager.h"
#include "TherapyPresets.h"

class CommandHandler {
public:
    CommandHandler(MagnetoController& ctrl, BatteryMonitor& battery, BLEManager& ble, StorageManager& storage);
    void handle(const String& rawJson);

private:
    MagnetoController& _ctrl;
    BatteryMonitor&    _battery;
    BLEManager&        _ble;
    StorageManager&    _storage;

    void processPreset(const JsonDocument& doc);
    void processStart(const JsonDocument& doc);
    void processPause();
    void processResume(const JsonDocument& doc);
    void processStop();
    void processSet(const JsonDocument& doc);
    void processStatus();
    bool prepareConfiguration(const JsonDocument& doc, ControlConfiguration& proposed, bool& changed);
    bool allowedFields(const JsonDocument& doc, std::initializer_list<const char*> fields);
    void reject(const char* field, const char* error = "INVALID_PARAM");
    void saveConfiguration();
};

#endif // COMMAND_HANDLER_H
