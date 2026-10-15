#pragma once
// Sólo sustituye periféricos/servicios; parser y controlador son los de producción.
#define BATTERY_MONITOR_H
#define BLE_MANAGER_H
#define STORAGE_MANAGER_H
#include "MagnetoTypes.h"
#include <vector>

class BatteryMonitor {
public:
    bool empty = false;
    bool isEmpty() const { return empty; }
    int getPercentage() const { return empty ? 0 : 80; }
    float getVoltage() const { return empty ? 0 : 2.0f; }
};
class BLEManager {
public:
    std::vector<std::string> notifications;
    void sendNotification(const String& message) { notifications.emplace_back(message.c_str()); }
};
class StorageManager {
public:
    int writes = 0;
    int duration = -1;
    void saveLastDuration(int minutes) { ++writes; duration=minutes; }
    void saveParams(int, const MagnetoChannelParams&) { ++writes; }
};
